/*
 * rcp.c  -  RCP over the pure-C SOME/IP stack (libsomeip). No C++.
 *
 * Core (transmit + discovery) and every method are ported 1:1 from the C++
 * client (LAN866XClientImpl) in liblan866x/lan866x_client.cpp:
 *   - request:  Fill_Header -> Fill_<field> (tag 0,1,2,..) -> Update_Length ->
 *               Transmit_Send   (retried on timeout, like REQUEST_RETRIES)
 *   - response: parsed in on_data_received() and routed via
 *               SOMEIP_Transmit_ReceivedResponse() to the pending buffer's
 *               callback (on_response), which stores the reply payload.
 *
 * PLATFORM: the SOMEIP_CB_* callbacks are provided by someip_stub_win.c on
 *   Windows; for MCU32 swap that file for an lwIP/FreeRTOS implementation.
 */
#include "rcp.h"
#include "someip.h"
#include <string.h>

#define MAXP  SOMEIP_TRANSMIT_MAX_PAYLOAD_LEN

extern uint8_t MULTICAST_IP[];

/* --- module state ------------------------------------------------------- */
static SOMETR_t *s_tr      = NULL;
static uint16_t  s_port    = 0u;
static uint16_t  s_session = 1u;

static rcp_endpoint_t s_eps[RCP_MAX_ENDPOINTS];
static uint8_t        s_epCount = 0u;
static uint8_t        s_sel     = 0u;
static uint32_t       s_timeoutMs = 1500u;   /* per-attempt response timeout */
static uint8_t        s_retries   = 3u;      /* extra attempts on RT_TIMEOUT  */

static volatile bool                   s_done = false;
static volatile enum SOMEIP_ReturnCode s_rc   = SOMEIP_E_TIMEOUT;
static uint8_t  s_rx[MAXP];
static uint16_t s_rxLen = 0u;
static uint8_t  s_scratch[MAXP];             /* request-parameter scratch     */

#ifdef _WIN32
#  include <windows.h>
#  define NAP() Sleep(2)
#else
#  include <time.h>
#  define NAP() do{ struct timespec t={0,2000000L}; nanosleep(&t,0);}while(0)
#endif

/* --- discovery event callback (mirrors OnSomeIpEvent) ------------------- */
static void on_event(enum SOMEIP_CB_Event evnt, struct SOMEIP_Server_Client *pSC,
                     struct SOMEIP_IpAddr *pIp, uint16_t receivedInstanceId,
                     struct SOMEIP_OptConfig *pConfig, void *eventData)
{
    (void)pConfig; (void)eventData;
    if (!pSC || !pIp) return;
    if (evnt == EV_CLIENT_SERVICE_AVAILABLE) {
        uint8_t i;
        for (i = 0; i < s_epCount; ++i)
            if (memcmp(s_eps[i].ip, pIp->sourceAddr, 4) == 0 &&
                s_eps[i].instanceId == receivedInstanceId) { s_eps[i].available = true; return; }
        if (s_epCount < RCP_MAX_ENDPOINTS) {
            rcp_endpoint_t *e = &s_eps[s_epCount++];
            memcpy(e->ip, pIp->sourceAddr, 4);
            e->port = pIp->port;
            e->serviceId = pSC->serviceId;
            e->instanceId = receivedInstanceId;
            e->available = true;
        }
    } else if (evnt == EV_CLIENT_SERVICE_STOPPED) {
        uint8_t i;
        for (i = 0; i < s_epCount; ++i)
            if (memcmp(s_eps[i].ip, pIp->sourceAddr, 4) == 0) s_eps[i].available = false;
    }
}

/* --- transmit-socket RX: route responses (mirrors OnDataReceived) ------- */
static enum SOMEIP_ReturnCode on_data_received(const uint8_t *b, uint16_t bLen,
                                               struct SOMEIP_IpAddr *pIp, void *rxTag)
{
    struct SOMEIP_Header par;
    uint16_t parsed = 0u;
    enum SOMEIP_ReturnCode rc;
    (void)rxTag;
    if (!pIp) return SOMEIP_E_NOT_REACHABLE;
    rc = SOMEIP_Parser_Read_Header(b, bLen, &par, &parsed);
    if (rc != SOMEIP_E_OK) return SOMEIP_E_MALFORMED_MESSAGE;
    if (par.serviceId == RCP_SERVICE_ID && par.interfaceVersion == 0x1u) {
        if (par.msgType == MSGTYPE_RESPONSE || par.msgType == MSGTYPE_ERROR) {
            SOMEIP_Transmit_ReceivedResponse(pIp->destinAddr, s_tr, par.sessionId,
                                             par.retCode, &b[parsed], (uint16_t)(bLen - parsed));
            return SOMEIP_E_OK;
        }
        return SOMEIP_E_OK;   /* notifications/events not used here */
    }
    return SOMEIP_E_OK;
}

/* --- response callback from the transmit layer -------------------------- */
static void on_response(struct SOMEIP_Transmit_Buffer *pBuf, bool ok,
                        enum SOMEIP_ReturnCode rc, const uint8_t *pRx, uint16_t rxLen)
{
    (void)pBuf;
    s_rc    = ok ? rc : SOMEIP_E_TIMEOUT;
    s_rxLen = (rxLen > MAXP) ? MAXP : rxLen;
    if (pRx && s_rxLen) memcpy(s_rx, pRx, s_rxLen);
    s_done = true;
}

/* --- one request attempt: header + params -> send -> wait --------------- */
static ReturnCode_t rcp_attempt(uint16_t methodId, const uint8_t *params, uint16_t plen)
{
    struct SOMEIP_Transmit_Buffer *tb;
    struct SOMEIP_Header h;
    uint16_t consumed = 0u, sid;
    uint32_t start;
    if (s_sel >= s_epCount) return RT_INTERNAL_ERROR;
    tb = SOMEIP_Transmit_GetBuffer(s_tr);
    if (!tb) return RT_INTERNAL_ERROR;
    sid = s_session++; if (s_session == 0u) s_session++;
    memset(&h, 0, sizeof(h));
    h.msgType = MSGTYPE_REQUEST; h.retCode = SOMEIP_E_OK; h.serviceId = RCP_SERVICE_ID;
    h.methodId = methodId; h.clientId = 0xaffeu; h.sessionId = sid; h.interfaceVersion = 0x1u;
    if (!SOMEIP_Generator_Fill_Header(&h, tb->payload, MAXP, &consumed)) {
        SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_PARAMETER_NOT_VALID;
    }
    if (plen) {
        if ((uint32_t)consumed + plen > MAXP) { SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_PARAMETER_NOT_VALID; }
        memcpy(&tb->payload[consumed], params, plen); consumed = (uint16_t)(consumed + plen);
    }
    if (!SOMEIP_Generator_Update_Length(consumed, tb->payload, MAXP)) {
        SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_PARAMETER_NOT_VALID;
    }
    tb->callback = on_response;
    tb->fireAndForget = false;
    tb->waitForSessionId = sid;
    tb->payloadLength = consumed;
    tb->udpPort = s_eps[s_sel].port;
    memcpy(tb->ipV4Addr, s_eps[s_sel].ip, 4);
    s_done = false;
    if (!SOMEIP_Transmit_Send(s_tr, tb)) {
        SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_SEND_ERROR;
    }
    start = SOMEIP_CB_GetTimeMS();
    while (!s_done && (SOMEIP_CB_GetTimeMS() - start) < s_timeoutMs) { rcp_poll(); NAP(); }
    if (!s_done) return RT_TIMEOUT;
    return (ReturnCode_t)s_rc;   /* SOMEIP_E_OK == 0 == RT_OK */
}

/* request with retry on timeout (like the C++ client's REQUEST_RETRIES loop).
 * On RT_OK the reply payload is left in s_rx[0..s_rxLen]. */
static ReturnCode_t rcp_xfer(uint16_t methodId, const uint8_t *params, uint16_t plen)
{
    ReturnCode_t rc = RT_TIMEOUT;
    uint8_t tries = (uint8_t)(s_retries + 1u);
    while (tries--) {
        rc = rcp_attempt(methodId, params, plen);
        if (rc != RT_TIMEOUT) break;   /* a real response (OK or error) -> done */
    }
    return rc;
}

/* --- discovery / lifecycle ---------------------------------------------- */
uint8_t rcp_get_endpoints(rcp_endpoint_t *out, uint8_t maxOut)
{
    uint8_t n = (s_epCount < maxOut) ? s_epCount : maxOut;
    if (out) memcpy(out, s_eps, n * sizeof(rcp_endpoint_t));
    return n;
}
bool rcp_select_endpoint(uint8_t index) { if (index >= s_epCount) return false; s_sel = index; return true; }

bool rcp_init(const uint8_t localIfIP[4])
{
    struct SOMEIP_Server_Client svc;
    (void)localIfIP;
    s_port = 0u;
    s_tr = SOMEIP_Transmit_Init(&s_port, on_data_received, NULL);
    if (!s_tr) return false;
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
    return SOMEIP_Client_AddService(&svc, true, false);
}

void rcp_poll(void) { SOMEIP_Transmit_CheckTimers(); }
bool rcp_is_ready(void) { return s_epCount > 0u; }
void rcp_set_timeout_ms(uint32_t ms) { s_timeoutMs = ms ? ms : 1u; }
void rcp_set_retries(uint8_t n) { s_retries = n; }

/* ======================== methods (1:1 with C++) ======================== */

ReturnCode_t rcp_get_status(GetStatusReply_t *out)
{
    uint16_t p = 0u, tag = 0u; const uint8_t *b; uint16_t n;
    ReturnCode_t rc = rcp_xfer(0x1002u, NULL, 0);
    if (rc != RT_OK) return rc;
    b = s_rx; n = s_rxLen;
    out->ActiveApplicationLength = sizeof(out->ActiveApplication);
    out->ChipIdentifierLength = sizeof(out->ChipIdentifier);
    out->RootApplicationVersionLength = sizeof(out->RootApplicationVersion);
    out->BootApplicationVersionLength = sizeof(out->BootApplicationVersion);
    out->BootConfigurationVersionLength = sizeof(out->BootConfigurationVersion);
    out->MainApplicationVersionLength = sizeof(out->MainApplicationVersion);
    out->MainConfigurationVersionLength = sizeof(out->MainConfigurationVersion);
    out->KeysVersionLength = sizeof(out->KeysVersion);
    bool ok =
        SOMEIP_Parser_Read_BLOB(&b[p], n - p, &tag, out->ActiveApplication, &out->ActiveApplicationLength, &p) &&
        SOMEIP_Parser_Read_BLOB(&b[p], n - p, &tag, out->ChipIdentifier, &out->ChipIdentifierLength, &p) &&
        SOMEIP_Parser_Read_BLOB(&b[p], n - p, &tag, out->RootApplicationVersion, &out->RootApplicationVersionLength, &p) &&
        SOMEIP_Parser_Read_BLOB(&b[p], n - p, &tag, out->BootApplicationVersion, &out->BootApplicationVersionLength, &p) &&
        SOMEIP_Parser_Read_BLOB(&b[p], n - p, &tag, out->BootConfigurationVersion, &out->BootConfigurationVersionLength, &p) &&
        SOMEIP_Parser_Read_BLOB(&b[p], n - p, &tag, out->MainApplicationVersion, &out->MainApplicationVersionLength, &p) &&
        SOMEIP_Parser_Read_BLOB(&b[p], n - p, &tag, out->MainConfigurationVersion, &out->MainConfigurationVersionLength, &p) &&
        SOMEIP_Parser_Read_UINT64(&b[p], n - p, &tag, &out->StartupInformation, &p) &&
        SOMEIP_Parser_Read_UINT64(&b[p], n - p, &tag, &out->UpTime, &p) &&
        SOMEIP_Parser_Read_UINT32(&b[p], n - p, &tag, &out->ComoVersion, &p) &&
        SOMEIP_Parser_Read_UINT32(&b[p], n - p, &tag, &out->ServiceVersion, &p) &&
        SOMEIP_Parser_Read_BLOB(&b[p], n - p, &tag, out->KeysVersion, &out->KeysVersionLength, &p);
    return ok ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_get_network_status(GetNetworkStatusReply_t *out)
{
    uint16_t p = 0u, tag = 0u; const uint8_t *b; uint16_t n;
    ReturnCode_t rc = rcp_xfer(0x1600u, NULL, 0);
    if (rc != RT_OK) return rc;
    b = s_rx; n = s_rxLen;
    bool ok =
        SOMEIP_Parser_Read_UINT32(&b[p], n - p, &tag, &out->EndpointIpV4Address, &p) &&
        SOMEIP_Parser_Read_UINT64(&b[p], n - p, &tag, &out->EndpointIpV6AddressHi, &p) &&
        SOMEIP_Parser_Read_UINT64(&b[p], n - p, &tag, &out->EndpointIpV6AddressLo, &p) &&
        SOMEIP_Parser_Read_UINT64(&b[p], n - p, &tag, &out->EndpointAddress, &p) &&
        SOMEIP_Parser_Read_UINT8(&b[p], n - p, &tag, &out->EndpointStatus, &p) &&
        SOMEIP_Parser_Read_UINT64(&b[p], n - p, &tag, &out->OaspiAddress, &p) &&
        SOMEIP_Parser_Read_UINT8(&b[p], n - p, &tag, &out->OaspiStatus, &p) &&
        SOMEIP_Parser_Read_UINT8(&b[p], n - p, &tag, &out->ArbitrationMode, &p) &&
        SOMEIP_Parser_Read_UINT8(&b[p], n - p, &tag, &out->PLCANodeId, &p);
    return ok ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_reboot(const char *name)
{
    uint8_t img[32]; uint16_t pl = 0u; size_t L = strlen(name);
    if (L + 4u > sizeof(img)) return RT_PARAMETER_NOT_VALID;
    img[0] = 0xEFu; img[1] = 0xBBu; img[2] = 0xBFu;        /* UTF-8 BOM */
    memcpy(&img[3], name, L); img[3 + L] = 0u;             /* name + NUL */
    if (!SOMEIP_Generator_Fill_BLOB(0, img, (uint16_t)(L + 4u), s_scratch, MAXP, &pl))
        return RT_PARAMETER_NOT_VALID;
    return rcp_xfer(0x1000u, s_scratch, pl);
}

ReturnCode_t rcp_release_digital_pins(const ReleaseDigitalPinsVar_t *in)
{
    uint16_t pl = 0u;
    if (!SOMEIP_Generator_Fill_BLOB(0, in->PinIdList, in->PinIdListLength, s_scratch, MAXP, &pl))
        return RT_PARAMETER_NOT_VALID;
    return rcp_xfer(0x1105u, s_scratch, pl);
}

ReturnCode_t rcp_open_gpio(const OpenGpioVar_t *in, OpenGpioReply_t *out)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc;
    if (!(SOMEIP_Generator_Fill_UINT8(0, in->PinIdGpio, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_UINT8(1, in->Direction, s_scratch, MAXP, &pl)))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1300u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    return SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->HandleGpio, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_set_gpio(const SetGpioVar_t *in)
{
    uint16_t pl = 0u;
    if (!SOMEIP_Generator_Fill_BLOB(0, in->GpioValues, in->GpioValuesLength, s_scratch, MAXP, &pl))
        return RT_PARAMETER_NOT_VALID;
    return rcp_xfer(0x1330u, s_scratch, pl);
}

ReturnCode_t rcp_get_gpio(GetGpioReply_t *out)
{
    uint16_t p = 0u, tag = 0u;
    ReturnCode_t rc = rcp_xfer(0x1332u, NULL, 0);
    if (rc != RT_OK) return rc;
    out->GpioValuesLength = sizeof(out->GpioValues);
    return SOMEIP_Parser_Read_BLOB(&s_rx[p], s_rxLen - p, &tag, out->GpioValues, &out->GpioValuesLength, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_open_i2c(const OpenI2CVar_t *in, OpenI2CReply_t *out)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc;
    if (!(SOMEIP_Generator_Fill_UINT8(0, in->PinIdSda, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_UINT8(1, in->PinIdScl, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_UINT8(2, in->ClockSpeed, s_scratch, MAXP, &pl)))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1200u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    return SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->HandleI2C, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_write_and_read_i2c(const WriteAndReadI2CVar_t *in, ReadI2CReply_t *out)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc;
    if (!(SOMEIP_Generator_Fill_UINT16(0, in->HandleI2C, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_UINT16(1, in->DeviceAddress, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_UINT16(2, in->ReadDataLength, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_UINT32(3, in->WriteId, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_BLOB(4, in->WriteData, in->WriteDataLength, s_scratch, MAXP, &pl)))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1208u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    out->ReadDataLength = sizeof(out->ReadData);
    return (SOMEIP_Parser_Read_UINT32(&s_rx[p], s_rxLen - p, &tag, &out->ReadId, &p) &&
            SOMEIP_Parser_Read_BLOB(&s_rx[p], s_rxLen - p, &tag, out->ReadData, &out->ReadDataLength, &p)) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_close_i2c(const CloseI2CVar_t *in)
{
    uint16_t pl = 0u;
    if (!SOMEIP_Generator_Fill_UINT16(0, in->HandleI2C, s_scratch, MAXP, &pl))
        return RT_PARAMETER_NOT_VALID;
    return rcp_xfer(0x1202u, s_scratch, pl);
}

ReturnCode_t rcp_open_spi(const OpenSpiVar_t *in, OpenSpiReply_t *out)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc;
    if (!(SOMEIP_Generator_Fill_UINT8(0, in->PinIdMiso, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_UINT8(1, in->PinIdSck, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_UINT8(2, in->PinIdCs, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_UINT8(3, in->PinIdMosi, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_UINT8(4, in->Mode, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_UINT32(5, in->ClockSpeed, s_scratch, MAXP, &pl)))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1500u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    return SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->HandleSpi, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_write_and_read_spi(const WriteAndReadSpiVar_t *in, WriteAndReadSpiReply_t *out)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc;
    if (!(SOMEIP_Generator_Fill_UINT16(0, in->HandleSpi, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_UINT16(1, in->ReadDataLength, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_UINT32(2, in->WriteId, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_BLOB(3, in->WriteData, in->WriteDataLength, s_scratch, MAXP, &pl)))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1508u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    out->ReadDataLength = sizeof(out->ReadData);
    return (SOMEIP_Parser_Read_UINT32(&s_rx[p], s_rxLen - p, &tag, &out->ReadId, &p) &&
            SOMEIP_Parser_Read_BLOB(&s_rx[p], s_rxLen - p, &tag, out->ReadData, &out->ReadDataLength, &p)) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_close_spi(const CloseSpiVar_t *in)
{
    uint16_t pl = 0u;
    if (!SOMEIP_Generator_Fill_UINT16(0, in->HandleSpi, s_scratch, MAXP, &pl))
        return RT_PARAMETER_NOT_VALID;
    return rcp_xfer(0x1502u, s_scratch, pl);
}

ReturnCode_t rcp_open_adc(const OpenAdcVar_t *in, OpenAdcReply_t *out)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc;
    if (!SOMEIP_Generator_Fill_UINT8(0, in->PinId, s_scratch, MAXP, &pl))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1700u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    return SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->HandleAdc, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_read_adc(const ReadAdcVar_t *in, ReadAdcReply_t *out)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc;
    if (!(SOMEIP_Generator_Fill_UINT16(0, in->HandleAdc, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_UINT8(1, in->ChannelSelecct, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_UINT8(2, in->VoltageReference, s_scratch, MAXP, &pl)))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1720u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    return (SOMEIP_Parser_Read_UINT8(&s_rx[p], s_rxLen - p, &tag, &out->Instance, &p) &&
            SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->ReadData, &p)) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_close_adc(const CloseAdcVar_t *in)
{
    uint16_t pl = 0u;
    if (!SOMEIP_Generator_Fill_UINT16(0, in->HandleAdc, s_scratch, MAXP, &pl))
        return RT_PARAMETER_NOT_VALID;
    return rcp_xfer(0x1702u, s_scratch, pl);
}

ReturnCode_t rcp_open_pwm(const OpenPwmVar_t *in, OpenPwmReply_t *out)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc;
    if (!(SOMEIP_Generator_Fill_UINT8(0, in->PinId, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_UINT32(1, in->IntervalTime, s_scratch, MAXP, &pl) &&
          SOMEIP_Generator_Fill_UINT32(2, in->DutyCycle, s_scratch, MAXP, &pl)))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1800u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    return SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->HandlePwm, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_close_pwm(const ClosePwmVar_t *in)
{
    uint16_t pl = 0u;
    if (!SOMEIP_Generator_Fill_UINT16(0, in->HandlePwm, s_scratch, MAXP, &pl))
        return RT_PARAMETER_NOT_VALID;
    return rcp_xfer(0x1802u, s_scratch, pl);
}
