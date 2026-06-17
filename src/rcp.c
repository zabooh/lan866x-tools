/*
 * rcp.c  -  RCP-Wrapper ueber den reinen C SOME/IP-Stack (libsomeip).
 *           Portable STM32-Vorlage. Strukturen/Sequenz gegen someip.h und
 *           LAN866XClientImpl (lan866x_client.cpp) verifiziert.
 *
 * Aufbau analog zum C++-Wrapper:
 *   Init      : SOMEIP_Transmit_Init(&port, on_data_received, ...) +
 *               SOMEIP_Client_AddService(serviceId=0xFF10, ...)
 *   Discovery : Event EV_CLIENT_SERVICE_AVAILABLE liefert pIp->sourceAddr/port
 *               + receivedInstanceId  -> Endpoint-Tabelle
 *   Methode   : CreateHeader -> SOMEIP_Generator_Fill_* -> Transmit (Ziel-IP/Port
 *               vom selektierten Endpoint)
 *
 * OFFEN (1 Stelle): on_data_received() muss eingehende SOME/IP-Pakete parsen
 *   und dispatchen -> SOMEIP_Client_DataReceived() (SD/Events) sowie
 *   SOMEIP_Transmit_ReceivedResponse() (Methoden-Antworten, per sessionId).
 *   Referenz-Implementierung: LAN866XClientImpl::OnDataReceived()
 *   in lan866x_client.cpp (~Zeile 3240). 1:1 uebernehmen.
 *
 * PLATTFORM: Die SOMEIP_CB_*-Callbacks (Socket/Sem/CritSec/SendUdp/Buffer)
 *   liefert auf Windows someip-stub.cpp + windows-udp-handler.c; auf STM32
 *   durch lwIP/FreeRTOS-Implementierung ersetzen (siehe PORTING.md).
 */
#include "rcp.h"
#include "someip.h"
#include <string.h>

#define MAXP  SOMEIP_TRANSMIT_MAX_PAYLOAD_LEN

/* Vom Stack erwartete SD-Multicast-Adresse (in main.c / discovery definiert). */
extern uint8_t MULTICAST_IP[];

/* --- Modulzustand ------------------------------------------------------- */
static SOMETR_t *s_tr      = NULL;
static uint16_t  s_port    = 0u;
static uint16_t  s_session = 1u;

static rcp_endpoint_t s_eps[RCP_MAX_ENDPOINTS];
static uint8_t        s_epCount = 0u;
static uint8_t        s_sel     = 0u;     /* aktiver Ziel-Endpoint */

/* letzte (synchron abgewartete) Methodenantwort */
static volatile bool                   s_done = false;
static volatile enum SOMEIP_ReturnCode s_rc   = SOMEIP_E_TIMEOUT;
static uint8_t  s_rx[MAXP];
static uint16_t s_rxLen = 0u;

/* --- Plattform-Pause im Warteloop -------------------------------------- */
#ifdef _WIN32
#  include <windows.h>
#  define NAP() Sleep(2)
#else
#  include <time.h>
#  define NAP() do{ struct timespec t={0,2000000L}; nanosleep(&t,0);}while(0)
#endif

/* --- Discovery-Event-Callback (Signatur gem. someip.h) ------------------ */
static void on_event(enum SOMEIP_CB_Event evnt,
                     struct SOMEIP_Server_Client *pSC,
                     struct SOMEIP_IpAddr *pIp,
                     uint16_t receivedInstanceId,
                     struct SOMEIP_OptConfig *pConfig,
                     void *eventData)
{
    (void)pConfig; (void)eventData;
    if (!pSC || !pIp) return;

    if (evnt == EV_CLIENT_SERVICE_AVAILABLE) {
        /* bereits bekannt? */
        for (uint8_t i = 0; i < s_epCount; ++i) {
            if (memcmp(s_eps[i].ip, pIp->sourceAddr, 4) == 0 &&
                s_eps[i].instanceId == receivedInstanceId) {
                s_eps[i].available = true; return;
            }
        }
        if (s_epCount < RCP_MAX_ENDPOINTS) {
            rcp_endpoint_t *e = &s_eps[s_epCount++];
            memcpy(e->ip, pIp->sourceAddr, 4);
            e->port = pIp->port;
            e->serviceId = pSC->serviceId;
            e->instanceId = receivedInstanceId;
            e->available = true;
        }
    } else if (evnt == EV_CLIENT_SERVICE_STOPPED) {
        for (uint8_t i = 0; i < s_epCount; ++i)
            if (memcmp(s_eps[i].ip, pIp->sourceAddr, 4) == 0)
                s_eps[i].available = false;
    }
}

/* --- RX: UDP-Daten in den Stack einspeisen + Antworten routen ----------- */
static enum SOMEIP_ReturnCode on_data_received(const uint8_t *b, uint16_t bLen,
                                               struct SOMEIP_IpAddr *pIp, void *rxTag)
{
    (void)rxTag;
    /* SD- und Event-Pfad: */
    enum SOMEIP_ReturnCode rc = SOMEIP_Client_DataReceived(b, bLen, pIp, NULL);
    /* >>> OFFEN: Methoden-Antworten parsen und an den wartenden Transmit-
     * Buffer routen: SOMEIP-Header parsen (someip-pars.h) -> sessionId/retCode
     * -> SOMEIP_Transmit_ReceivedResponse(srcIp, s_tr, sessionId, retCode,
     *    &payload, payloadLen). Vorlage: lan866x_client.cpp::OnDataReceived. */
    return rc;
}

/* --- Methodenantwort-Callback (vom Transmit-Layer) ---------------------- */
static void on_response(struct SOMEIP_Transmit_Buffer *pBuf, bool ok,
                        enum SOMEIP_ReturnCode rc,
                        const uint8_t *pRx, uint16_t rxLen)
{
    (void)pBuf;
    s_rc   = ok ? rc : SOMEIP_E_TIMEOUT;
    s_rxLen = (rxLen > MAXP) ? MAXP : rxLen;
    if (pRx && s_rxLen) memcpy(s_rx, pRx, s_rxLen);
    s_done = true;
}

/* --- Generischer Methodenaufruf (mirror von SetGpio im C++-Wrapper) ----- */
static bool rcp_call(uint16_t methodId, bool fireAndForget,
                     const uint8_t *blob, uint16_t blobLen,
                     uint8_t *outRx, uint16_t *outRxLen)
{
    if (s_sel >= s_epCount) return false;
    struct SOMEIP_Transmit_Buffer *tb = SOMEIP_Transmit_GetBuffer(s_tr);
    if (!tb) return false;

    /* Header (Werte wie LAN866XClientImpl::CreateHeader) */
    uint16_t sid = s_session++; if (s_session == 0u) s_session++;
    struct SOMEIP_Header h;
    memset(&h, 0, sizeof(h));
    h.length = 0u;
    h.msgType = fireAndForget ? MSGTYPE_REQUEST_NO_RETURN : MSGTYPE_REQUEST;
    h.retCode = SOMEIP_E_OK;
    h.serviceId = RCP_SERVICE_ID;
    h.methodId = methodId;
    h.clientId = 0xaffeu;
    h.sessionId = sid;
    h.interfaceVersion = 0x1u;
    h.generateEvent = false;

    uint16_t consumed = 0u;
    bool ok = SOMEIP_Generator_Fill_Header(&h, tb->payload, MAXP, &consumed);
    if (ok && blob && blobLen)
        ok = SOMEIP_Generator_Fill_BLOB(0u, blob, blobLen,
                                        &tb->payload[consumed], (uint16_t)(MAXP - consumed), &consumed);
    ok = ok && SOMEIP_Generator_Update_Length(consumed, tb->payload, MAXP);
    if (!ok) { SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return false; }

    /* Transmit-Buffer befuellen (wie TransmitBuffer im C++-Wrapper) */
    tb->callback        = fireAndForget ? NULL : on_response;
    tb->fireAndForget   = fireAndForget;
    tb->waitForSessionId = sid;
    tb->payloadLength   = consumed;
    tb->udpPort         = s_eps[s_sel].port;
    memcpy(tb->ipV4Addr, s_eps[s_sel].ip, 4);

    s_done = false;
    if (!SOMEIP_Transmit_Send(s_tr, tb)) return false;
    if (fireAndForget) return true;

    for (int i = 0; i < 200 && !s_done; ++i) { rcp_poll(); NAP(); }
    if (!s_done) return false;
    if (outRx && outRxLen) {
        uint16_t n = (s_rxLen < *outRxLen) ? s_rxLen : *outRxLen;
        memcpy(outRx, s_rx, n); *outRxLen = n;
    }
    return (s_rc == SOMEIP_E_OK);
}

/* --- Discovery-API ------------------------------------------------------ */
uint8_t rcp_get_endpoints(rcp_endpoint_t *out, uint8_t maxOut)
{
    uint8_t n = (s_epCount < maxOut) ? s_epCount : maxOut;
    if (out) memcpy(out, s_eps, n * sizeof(rcp_endpoint_t));
    return n;
}
bool rcp_select_endpoint(uint8_t index)
{
    if (index >= s_epCount) return false;
    s_sel = index; return true;
}

/* --- Lebenszyklus ------------------------------------------------------- */
bool rcp_init(const uint8_t localIfIP[4])
{
    (void)localIfIP; /* Interface-Wahl macht der Plattform-Stub (SOMEIP_CB_GetLocalIpAddr) */
    s_port = 0u;     /* zufaelligen Port waehlen lassen */
    s_tr = SOMEIP_Transmit_Init(&s_port, on_data_received, NULL);
    if (!s_tr) return false;

    struct SOMEIP_Server_Client svc;
    memset(&svc, 0, sizeof(svc));
    svc.pEventCb = on_event;
    svc.serviceId = RCP_SERVICE_ID;
    svc.instanceId = 0x1u;
    svc.majorVersion = 1u;
    svc.minorVersion = 1u;
    svc.ttl = 5u;
    svc.clientId = 0xaffeu;
    svc.eventGroupId = 0u;
    svc.eventHandlingEnabled = false;   /* Methoden, kein Event */
    svc.ipAddr.port = s_port;
    /* lokale IP setzt der Stub; Subnetz-Match via MULTICAST_IP/Stub */
    return SOMEIP_Client_AddService(&svc, /*requested*/ true, /*subscribe*/ false);
}

void rcp_poll(void)
{
    SOMEIP_Client_CheckTimers();
    SOMEIP_Transmit_CheckTimers();
}

bool rcp_is_ready(void) { return s_epCount > 0u; }

/* --- GPIO / I2C / SPI (Method-IDs verifiziert; Param-Layout [V3] pruefen) */
bool rcp_open_gpio(const uint8_t *pinIds, uint8_t count)
{ return rcp_call(RCP_M_OPEN_GPIO, false, pinIds, count, NULL, NULL); }

bool rcp_set_gpio(const uint8_t *gpioValues, uint8_t len)
{ return rcp_call(RCP_M_SET_GPIO, false, gpioValues, len, NULL, NULL); }   /* BLOB(0): verifiziert */

bool rcp_get_gpio(uint8_t *outValues, uint8_t *outLen)
{ uint16_t n = outLen ? *outLen : 0; bool r = rcp_call(RCP_M_GET_GPIO, false, NULL, 0, outValues, &n);
  if (outLen) *outLen = (uint8_t)n; return r; }                            /* [V4] Response parsen */

bool rcp_open_i2c(uint8_t pinSda, uint8_t pinScl, uint32_t clockHz)
{ uint8_t a[6]; a[0]=pinSda; a[1]=pinScl; memcpy(&a[2],&clockHz,4);
  return rcp_call(RCP_M_OPEN_I2C, false, a, sizeof(a), NULL, NULL); }       /* [V3] */

bool rcp_write_read_i2c(uint8_t devAddr, const uint8_t *tx, uint16_t txLen,
                        uint8_t *rx, uint16_t rxLen)
{ uint8_t b[260]; uint16_t n=0; b[n++]=devAddr; b[n++]=(uint8_t)rxLen; b[n++]=(uint8_t)txLen;
  if (tx&&txLen){ memcpy(&b[n],tx,txLen); n+=txLen; }
  uint16_t out=rxLen; return rcp_call(RCP_M_WRITE_AND_READ_I2C, false, b, n, rx, &out); } /* [V3] */

bool rcp_open_spi(uint8_t pinMiso, uint8_t pinSck, uint8_t pinCs, uint8_t pinMosi,
                  uint8_t mode, uint32_t clockHz)
{ uint8_t a[8]; a[0]=pinMiso; a[1]=pinSck; a[2]=pinCs; a[3]=pinMosi; a[4]=mode; memcpy(&a[5],&clockHz,3);
  return rcp_call(RCP_M_OPEN_SPI, false, a, sizeof(a), NULL, NULL); }       /* [V3] */

bool rcp_write_read_spi(const uint8_t *tx, uint16_t txLen, uint8_t *rx, uint16_t rxLen)
{ uint8_t b[260]; uint16_t n=0; b[n++]=(uint8_t)rxLen;
  if (tx&&txLen){ memcpy(&b[n],tx,txLen); n+=txLen; }
  uint16_t out=rxLen; return rcp_call(RCP_M_WRITE_AND_READ_SPI, false, b, n, rx, &out); } /* [V3] spi-demo.cpp */
