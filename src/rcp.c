/*
 * rcp.c  -  RCP wrapper over the pure-C SOME/IP stack (libsomeip).
 *           Portable MCU32 template. Structs/sequence verified against
 *           someip.h and LAN866XClientImpl (lan866x_client.cpp).
 *
 * Layout analogous to the C++ wrapper:
 *   Init      : SOMEIP_Transmit_Init(&port, on_data_received, ...) +
 *               SOMEIP_Client_AddService(serviceId=0xFF10, ...)
 *   Discovery : event EV_CLIENT_SERVICE_AVAILABLE provides pIp->sourceAddr/port
 *               + receivedInstanceId  -> endpoint table
 *   Method    : CreateHeader -> SOMEIP_Generator_Fill_* -> Transmit (target IP/port
 *               of the selected endpoint)
 *
 * OPEN (1 spot): on_data_received() must parse incoming SOME/IP packets and
 *   dispatch -> SOMEIP_Client_DataReceived() (SD/events) and
 *   SOMEIP_Transmit_ReceivedResponse() (method responses, by sessionId).
 *   Reference implementation: LAN866XClientImpl::OnDataReceived()
 *   in lan866x_client.cpp (~line 3240). Adopt 1:1.
 *
 * PLATFORM: the SOMEIP_CB_* callbacks (socket/sem/critsec/SendUdp/buffer) are
 *   provided on Windows by someip-stub.cpp + windows-udp-handler.c; on MCU32
 *   replace with an lwIP/FreeRTOS implementation (see PORTING.md).
 */
#include "rcp.h"
#include "someip.h"
#include <string.h>
#ifdef RCP_DEBUG
#  include <stdio.h>   /* only for the optional [tx]/[rx] trace below */
#endif

#define MAXP  SOMEIP_TRANSMIT_MAX_PAYLOAD_LEN

/* SD multicast address expected by the stack (defined in main.c / discovery). */
extern uint8_t MULTICAST_IP[];

/* --- module state ------------------------------------------------------- */
static SOMETR_t *s_tr      = NULL;
static uint16_t  s_port    = 0u;
static uint16_t  s_session = 1u;

static rcp_endpoint_t s_eps[RCP_MAX_ENDPOINTS];
static uint8_t        s_epCount = 0u;
static uint8_t        s_sel     = 0u;     /* active target endpoint */

/* last (synchronously awaited) method response */
static volatile bool                   s_done = false;
static volatile enum SOMEIP_ReturnCode s_rc   = SOMEIP_E_TIMEOUT;
static uint8_t  s_rx[MAXP];
static uint16_t s_rxLen = 0u;

/* --- platform pause in the wait loop ----------------------------------- */
#ifdef _WIN32
#  include <windows.h>
#  define NAP() Sleep(2)
#else
#  include <time.h>
#  define NAP() do{ struct timespec t={0,2000000L}; nanosleep(&t,0);}while(0)
#endif

/* --- discovery event callback (signature per someip.h) ------------------ */
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
        /* already known? */
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

/* --- RX: route method responses to the waiting transmit buffer ----------
 * This is the RX callback of the *transmit* socket (SOMEIP_Transmit_Init).
 * It mirrors LAN866XClientImpl::OnDataReceived 1:1: parse the SOME/IP header
 * and, for RESPONSE/ERROR messages of our service, hand the payload to
 * SOMEIP_Transmit_ReceivedResponse() which matches it (by remote IP +
 * sessionId) to the pending buffer and fires its callback synchronously.
 *
 * The Service-Discovery path is handled on a *separate*, internally-opened
 * socket whose RX callback is the stack's own SD handler - we must NOT call
 * SOMEIP_Client_DataReceived() here (the C++ client does not either).
 *
 * Stub IP convention (see platform stub OnUdpRx): pIp->destinAddr holds the
 * remote peer (the endpoint), pIp->sourceAddr the local interface. */
static enum SOMEIP_ReturnCode on_data_received(const uint8_t *b, uint16_t bLen,
                                               struct SOMEIP_IpAddr *pIp, void *rxTag)
{
    struct SOMEIP_Header par;
    uint16_t parsed = 0u;
    enum SOMEIP_ReturnCode rc;
    (void)rxTag;
    if (!pIp) return SOMEIP_E_NOT_REACHABLE;

    rc = SOMEIP_Parser_Read_Header(b, bLen, &par, &parsed);
#ifdef RCP_DEBUG
    fprintf(stderr, "[rx] %u bytes from %u.%u.%u.%u svc=%04X mid=%04X mt=%02X sid=%u rcHdr=%d\n",
            bLen, pIp->destinAddr[0],pIp->destinAddr[1],pIp->destinAddr[2],pIp->destinAddr[3],
            par.serviceId, par.methodId, par.msgType, par.sessionId, (int)rc);
#endif
    if (rc != SOMEIP_E_OK) return SOMEIP_E_MALFORMED_MESSAGE;

    if (par.serviceId == RCP_SERVICE_ID && par.interfaceVersion == 0x1u) {
        if (par.msgType == MSGTYPE_RESPONSE || par.msgType == MSGTYPE_ERROR) {
            SOMEIP_Transmit_ReceivedResponse(pIp->destinAddr, s_tr, par.sessionId,
                                             par.retCode, &b[parsed],
                                             (uint16_t)(bLen - parsed));
            return SOMEIP_E_OK;
        }
        /* MSGTYPE_NOTIFICATION (events 0x8000/0x8010/0x8020): not used by this
         * minimal host - decode here later if events are needed. */
        return SOMEIP_E_OK;
    }
    return SOMEIP_E_OK;   /* not our service */
}

/* --- method response callback (from the transmit layer) ----------------- */
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

/* --- generic method call (mirror of SetGpio in the C++ wrapper) --------- */
static bool rcp_call(uint16_t methodId, bool fireAndForget,
                     const uint8_t *blob, uint16_t blobLen,
                     uint8_t *outRx, uint16_t *outRxLen)
{
    if (s_sel >= s_epCount) return false;
    struct SOMEIP_Transmit_Buffer *tb = SOMEIP_Transmit_GetBuffer(s_tr);
    if (!tb) return false;

    /* header (values like LAN866XClientImpl::CreateHeader) */
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

    /* fill transmit buffer (like TransmitBuffer in the C++ wrapper) */
    tb->callback        = fireAndForget ? NULL : on_response;
    tb->fireAndForget   = fireAndForget;
    tb->waitForSessionId = sid;
    tb->payloadLength   = consumed;
    tb->udpPort         = s_eps[s_sel].port;
    memcpy(tb->ipV4Addr, s_eps[s_sel].ip, 4);

    s_done = false;
    if (!SOMEIP_Transmit_Send(s_tr, tb)) {
#ifdef RCP_DEBUG
        fprintf(stderr, "[tx] Transmit_Send FAILED (ip=%u.%u.%u.%u port=%u len=%u)\n",
                tb->ipV4Addr[0],tb->ipV4Addr[1],tb->ipV4Addr[2],tb->ipV4Addr[3], tb->udpPort, consumed);
#endif
        return false;
    }
#ifdef RCP_DEBUG
    fprintf(stderr, "[tx] sent mid=%04X sid=%u -> %u.%u.%u.%u:%u len=%u\n",
            methodId, sid, tb->ipV4Addr[0],tb->ipV4Addr[1],tb->ipV4Addr[2],tb->ipV4Addr[3], tb->udpPort, consumed);
#endif
    if (fireAndForget) return true;

    for (int i = 0; i < 1500 && !s_done; ++i) { rcp_poll(); NAP(); }
    if (!s_done) return false;
    if (outRx && outRxLen) {
        uint16_t n = (s_rxLen < *outRxLen) ? s_rxLen : *outRxLen;
        memcpy(outRx, s_rx, n); *outRxLen = n;
    }
    return (s_rc == SOMEIP_E_OK);
}

/* --- discovery API ------------------------------------------------------ */
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

/* --- lifecycle ---------------------------------------------------------- */
bool rcp_init(const uint8_t localIfIP[4])
{
    (void)localIfIP; /* interface choice is done by the platform stub (SOMEIP_CB_GetLocalIpAddr) */
    s_port = 0u;     /* let the stack pick a random port */
    s_tr = SOMEIP_Transmit_Init(&s_port, on_data_received, NULL);
    if (!s_tr) return false;

    /* Values mirror LAN866XClientImpl::Init() so discovery behaves identically. */
    struct SOMEIP_Server_Client svc;
    memset(&svc, 0, sizeof(svc));
    svc.pEventCb = on_event;
    svc.serviceId = RCP_SERVICE_ID;
    svc.instanceId = 0x1u;
    svc.majorVersion = 1u;
    svc.minorVersion = 1u;
    svc.ttl = 2u;
    svc.clientId = 0xaffeu;
    svc.eventGroupId = 0x2000u;
    svc.eventHandlingEnabled = true;
    svc.ipAddr.port = s_port;
    svc.cbData = NULL;
    /* local IP is set by the stub; subnet match via MULTICAST_IP/stub.
     * subscribe=false: we use methods, not events. */
    return SOMEIP_Client_AddService(&svc, /*requested*/ true, /*subscribe*/ false);
}

void rcp_poll(void)
{
    /* SD + client timers are serviced by the platform stub's service thread
     * (Windows) / the SOME/IP task (MCU32). Here we only service the transmit
     * layer for request timeout handling. Responses themselves arrive async on
     * the RX thread and fire on_response() synchronously. */
    SOMEIP_Transmit_CheckTimers();
}

bool rcp_is_ready(void) { return s_epCount > 0u; }

/* --- system: GetStatus (empty request; raw response payload) ------------ */
bool rcp_get_status(uint8_t *outRx, uint16_t *outRxLen)
{ return rcp_call(RCP_M_GET_STATUS, false, NULL, 0, outRx, outRxLen); }

/* --- GPIO / I2C / SPI (method IDs verified; check param layout [V3]) ---- */
bool rcp_open_gpio(const uint8_t *pinIds, uint8_t count)
{ return rcp_call(RCP_M_OPEN_GPIO, false, pinIds, count, NULL, NULL); }

bool rcp_set_gpio(const uint8_t *gpioValues, uint8_t len)
{ return rcp_call(RCP_M_SET_GPIO, false, gpioValues, len, NULL, NULL); }   /* BLOB(0): verified */

bool rcp_get_gpio(uint8_t *outValues, uint8_t *outLen)
{ uint16_t n = outLen ? *outLen : 0; bool r = rcp_call(RCP_M_GET_GPIO, false, NULL, 0, outValues, &n);
  if (outLen) *outLen = (uint8_t)n; return r; }                            /* [V4] parse response */

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

/* --- ADC (OpenAdcVar_t/ReadAdcVar_t/ReadAdcReply_t in lan866x_common.h) -- */
bool rcp_open_adc(void)
{ uint8_t a[1]; a[0]=0u; /* PinId always 0 (analog) */
  return rcp_call(RCP_M_OPEN_ADC, false, a, sizeof(a), NULL, NULL); }          /* [V3] */

bool rcp_read_adc(uint8_t channel, uint8_t vref, uint16_t *out)
{ uint8_t a[2]; a[0]=channel; a[1]=vref;   /* handle tracking is part of [V4] */
  uint8_t rx[4]; uint16_t n=sizeof(rx);
  bool r = rcp_call(RCP_M_READ_ADC, false, a, sizeof(a), rx, &n);              /* [V4] ReadAdcReply_t */
  if (r && out && n>=3) *out = (uint16_t)(rx[1] | (rx[2]<<8));  /* {Instance, ReadData} */
  return r; }

/* --- PWM (OpenPwmVar_t/WritePwmVar_t in lan866x_common.h) ---------------- */
bool rcp_open_pwm(uint8_t pin, uint32_t intervalNs, uint32_t dutyQ31)
{ uint8_t a[9]; a[0]=pin; memcpy(&a[1],&intervalNs,4); memcpy(&a[5],&dutyQ31,4);
  return rcp_call(RCP_M_OPEN_PWM, false, a, sizeof(a), NULL, NULL); }          /* [V3] */

bool rcp_write_pwm(uint32_t dutyQ31)
{ uint8_t a[4]; memcpy(&a[0],&dutyQ31,4);  /* handle tracking is part of [V4] */
  return rcp_call(RCP_M_WRITE_PWM, false, a, sizeof(a), NULL, NULL); }         /* [V3] */
