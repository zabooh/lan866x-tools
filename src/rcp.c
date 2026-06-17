/*
 * rcp.c  -  RCP over the pure-C SOME/IP stack (libsomeip). No C++.
 *
 * Core (transmit + discovery) and every method are ported 1:1 from the C++
 * client (LAN866XClientImpl) in liblan866x/lan866x_client.cpp:
 *   - request:  CreateHeader -> Fill_Header -> Fill_<field> (tag 0,1,2,..) ->
 *               Update_Length -> Transmit_Send
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
#ifdef RCP_DEBUG
#  include <stdio.h>
#endif

#define MAXP  SOMEIP_TRANSMIT_MAX_PAYLOAD_LEN

extern uint8_t MULTICAST_IP[];

/* --- module state ------------------------------------------------------- */
static SOMETR_t *s_tr      = NULL;
static uint16_t  s_port    = 0u;
static uint16_t  s_session = 1u;

static rcp_endpoint_t s_eps[RCP_MAX_ENDPOINTS];
static uint8_t        s_epCount = 0u;
static uint8_t        s_sel     = 0u;
static uint32_t       s_timeoutMs = 2000u;   /* per-request response timeout */

static volatile bool                   s_done = false;
static volatile enum SOMEIP_ReturnCode s_rc   = SOMEIP_E_TIMEOUT;
static uint8_t  s_rx[MAXP];
static uint16_t s_rxLen = 0u;

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

/* --- request helpers ---------------------------------------------------- */
static struct SOMEIP_Transmit_Buffer *req_begin(uint16_t methodId,
                                                uint16_t *pConsumed, uint16_t *pSid)
{
    struct SOMEIP_Transmit_Buffer *tb;
    struct SOMEIP_Header h;
    uint16_t consumed = 0u, sid;
    if (s_sel >= s_epCount) return NULL;
    tb = SOMEIP_Transmit_GetBuffer(s_tr);
    if (!tb) return NULL;
    sid = s_session++; if (s_session == 0u) s_session++;
    memset(&h, 0, sizeof(h));
    h.length = 0u; h.msgType = MSGTYPE_REQUEST; h.retCode = SOMEIP_E_OK;
    h.serviceId = RCP_SERVICE_ID; h.methodId = methodId; h.clientId = 0xaffeu;
    h.sessionId = sid; h.interfaceVersion = 0x1u; h.generateEvent = false;
    if (!SOMEIP_Generator_Fill_Header(&h, tb->payload, MAXP, &consumed)) {
        SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return NULL;
    }
    *pConsumed = consumed; *pSid = sid;
    return tb;
}

static ReturnCode_t req_finish(struct SOMEIP_Transmit_Buffer *tb, uint16_t consumed, uint16_t sid)
{
    uint32_t start;
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
    /* real-time deadline (Sleep granularity is coarse on Windows, so do not
     * count iterations). The response arrives async and sets s_done. */
    start = SOMEIP_CB_GetTimeMS();
    while (!s_done && (SOMEIP_CB_GetTimeMS() - start) < s_timeoutMs) { rcp_poll(); NAP(); }
    if (!s_done) return RT_TIMEOUT;
    return (ReturnCode_t)s_rc;   /* SOMEIP_E_OK == 0 == RT_OK */
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

/* ======================== methods (1:1 with C++) ======================== */

ReturnCode_t rcp_get_status(GetStatusReply_t *out)
{
    uint16_t c, sid, p = 0u, tag = 0u; const uint8_t *b; uint16_t n;
    struct SOMEIP_Transmit_Buffer *tb = req_begin(0x1002u, &c, &sid);
    ReturnCode_t rc;
    if (!tb) return RT_INTERNAL_ERROR;
    rc = req_finish(tb, c, sid);
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
    uint16_t c, sid, p = 0u, tag = 0u; const uint8_t *b; uint16_t n;
    struct SOMEIP_Transmit_Buffer *tb = req_begin(0x1600u, &c, &sid);
    ReturnCode_t rc;
    if (!tb) return RT_INTERNAL_ERROR;
    rc = req_finish(tb, c, sid);
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

ReturnCode_t rcp_release_digital_pins(const ReleaseDigitalPinsVar_t *in)
{
    uint16_t c, sid;
    struct SOMEIP_Transmit_Buffer *tb = req_begin(0x1105u, &c, &sid);
    if (!tb) return RT_INTERNAL_ERROR;
    if (!SOMEIP_Generator_Fill_BLOB(0, in->PinIdList, in->PinIdListLength, &tb->payload[c], MAXP - c, &c))
        { SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_PARAMETER_NOT_VALID; }
    return req_finish(tb, c, sid);
}

ReturnCode_t rcp_open_gpio(const OpenGpioVar_t *in, OpenGpioReply_t *out)
{
    uint16_t c, sid, p = 0u, tag = 0u;
    struct SOMEIP_Transmit_Buffer *tb = req_begin(0x1300u, &c, &sid);
    ReturnCode_t rc;
    if (!tb) return RT_INTERNAL_ERROR;
    if (!(SOMEIP_Generator_Fill_UINT8(0, in->PinIdGpio, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_UINT8(1, in->Direction, &tb->payload[c], MAXP - c, &c)))
        { SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_PARAMETER_NOT_VALID; }
    rc = req_finish(tb, c, sid);
    if (rc != RT_OK) return rc;
    return SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->HandleGpio, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_set_gpio(const SetGpioVar_t *in)
{
    uint16_t c, sid;
    struct SOMEIP_Transmit_Buffer *tb = req_begin(0x1330u, &c, &sid);
    if (!tb) return RT_INTERNAL_ERROR;
    if (!SOMEIP_Generator_Fill_BLOB(0, in->GpioValues, in->GpioValuesLength, &tb->payload[c], MAXP - c, &c))
        { SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_PARAMETER_NOT_VALID; }
    return req_finish(tb, c, sid);
}

ReturnCode_t rcp_get_gpio(GetGpioReply_t *out)
{
    uint16_t c, sid, p = 0u, tag = 0u;
    struct SOMEIP_Transmit_Buffer *tb = req_begin(0x1332u, &c, &sid);
    ReturnCode_t rc;
    if (!tb) return RT_INTERNAL_ERROR;
    rc = req_finish(tb, c, sid);
    if (rc != RT_OK) return rc;
    out->GpioValuesLength = sizeof(out->GpioValues);
    return SOMEIP_Parser_Read_BLOB(&s_rx[p], s_rxLen - p, &tag, out->GpioValues, &out->GpioValuesLength, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_open_i2c(const OpenI2CVar_t *in, OpenI2CReply_t *out)
{
    uint16_t c, sid, p = 0u, tag = 0u;
    struct SOMEIP_Transmit_Buffer *tb = req_begin(0x1200u, &c, &sid);
    ReturnCode_t rc;
    if (!tb) return RT_INTERNAL_ERROR;
    if (!(SOMEIP_Generator_Fill_UINT8(0, in->PinIdSda, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_UINT8(1, in->PinIdScl, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_UINT8(2, in->ClockSpeed, &tb->payload[c], MAXP - c, &c)))
        { SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_PARAMETER_NOT_VALID; }
    rc = req_finish(tb, c, sid);
    if (rc != RT_OK) return rc;
    return SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->HandleI2C, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_write_and_read_i2c(const WriteAndReadI2CVar_t *in, ReadI2CReply_t *out)
{
    uint16_t c, sid, p = 0u, tag = 0u;
    struct SOMEIP_Transmit_Buffer *tb = req_begin(0x1208u, &c, &sid);
    ReturnCode_t rc;
    if (!tb) return RT_INTERNAL_ERROR;
    if (!(SOMEIP_Generator_Fill_UINT16(0, in->HandleI2C, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_UINT16(1, in->DeviceAddress, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_UINT16(2, in->ReadDataLength, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_UINT32(3, in->WriteId, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_BLOB(4, in->WriteData, in->WriteDataLength, &tb->payload[c], MAXP - c, &c)))
        { SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_PARAMETER_NOT_VALID; }
    rc = req_finish(tb, c, sid);
    if (rc != RT_OK) return rc;
    out->ReadDataLength = sizeof(out->ReadData);
    return (SOMEIP_Parser_Read_UINT32(&s_rx[p], s_rxLen - p, &tag, &out->ReadId, &p) &&
            SOMEIP_Parser_Read_BLOB(&s_rx[p], s_rxLen - p, &tag, out->ReadData, &out->ReadDataLength, &p)) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_close_i2c(const CloseI2CVar_t *in)
{
    uint16_t c, sid;
    struct SOMEIP_Transmit_Buffer *tb = req_begin(0x1202u, &c, &sid);
    if (!tb) return RT_INTERNAL_ERROR;
    if (!SOMEIP_Generator_Fill_UINT16(0, in->HandleI2C, &tb->payload[c], MAXP - c, &c))
        { SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_PARAMETER_NOT_VALID; }
    return req_finish(tb, c, sid);
}

ReturnCode_t rcp_open_spi(const OpenSpiVar_t *in, OpenSpiReply_t *out)
{
    uint16_t c, sid, p = 0u, tag = 0u;
    struct SOMEIP_Transmit_Buffer *tb = req_begin(0x1500u, &c, &sid);
    ReturnCode_t rc;
    if (!tb) return RT_INTERNAL_ERROR;
    if (!(SOMEIP_Generator_Fill_UINT8(0, in->PinIdMiso, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_UINT8(1, in->PinIdSck, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_UINT8(2, in->PinIdCs, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_UINT8(3, in->PinIdMosi, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_UINT8(4, in->Mode, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_UINT32(5, in->ClockSpeed, &tb->payload[c], MAXP - c, &c)))
        { SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_PARAMETER_NOT_VALID; }
    rc = req_finish(tb, c, sid);
    if (rc != RT_OK) return rc;
    return SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->HandleSpi, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_write_and_read_spi(const WriteAndReadSpiVar_t *in, WriteAndReadSpiReply_t *out)
{
    uint16_t c, sid, p = 0u, tag = 0u;
    struct SOMEIP_Transmit_Buffer *tb = req_begin(0x1508u, &c, &sid);
    ReturnCode_t rc;
    if (!tb) return RT_INTERNAL_ERROR;
    if (!(SOMEIP_Generator_Fill_UINT16(0, in->HandleSpi, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_UINT16(1, in->ReadDataLength, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_UINT32(2, in->WriteId, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_BLOB(3, in->WriteData, in->WriteDataLength, &tb->payload[c], MAXP - c, &c)))
        { SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_PARAMETER_NOT_VALID; }
    rc = req_finish(tb, c, sid);
    if (rc != RT_OK) return rc;
    out->ReadDataLength = sizeof(out->ReadData);
    return (SOMEIP_Parser_Read_UINT32(&s_rx[p], s_rxLen - p, &tag, &out->ReadId, &p) &&
            SOMEIP_Parser_Read_BLOB(&s_rx[p], s_rxLen - p, &tag, out->ReadData, &out->ReadDataLength, &p)) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_close_spi(const CloseSpiVar_t *in)
{
    uint16_t c, sid;
    struct SOMEIP_Transmit_Buffer *tb = req_begin(0x1502u, &c, &sid);
    if (!tb) return RT_INTERNAL_ERROR;
    if (!SOMEIP_Generator_Fill_UINT16(0, in->HandleSpi, &tb->payload[c], MAXP - c, &c))
        { SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_PARAMETER_NOT_VALID; }
    return req_finish(tb, c, sid);
}

ReturnCode_t rcp_open_adc(const OpenAdcVar_t *in, OpenAdcReply_t *out)
{
    uint16_t c, sid, p = 0u, tag = 0u;
    struct SOMEIP_Transmit_Buffer *tb = req_begin(0x1700u, &c, &sid);
    ReturnCode_t rc;
    if (!tb) return RT_INTERNAL_ERROR;
    if (!SOMEIP_Generator_Fill_UINT8(0, in->PinId, &tb->payload[c], MAXP - c, &c))
        { SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_PARAMETER_NOT_VALID; }
    rc = req_finish(tb, c, sid);
    if (rc != RT_OK) return rc;
    return SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->HandleAdc, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_read_adc(const ReadAdcVar_t *in, ReadAdcReply_t *out)
{
    uint16_t c, sid, p = 0u, tag = 0u;
    struct SOMEIP_Transmit_Buffer *tb = req_begin(0x1720u, &c, &sid);
    ReturnCode_t rc;
    if (!tb) return RT_INTERNAL_ERROR;
    if (!(SOMEIP_Generator_Fill_UINT16(0, in->HandleAdc, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_UINT8(1, in->ChannelSelecct, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_UINT8(2, in->VoltageReference, &tb->payload[c], MAXP - c, &c)))
        { SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_PARAMETER_NOT_VALID; }
    rc = req_finish(tb, c, sid);
    if (rc != RT_OK) return rc;
    return (SOMEIP_Parser_Read_UINT8(&s_rx[p], s_rxLen - p, &tag, &out->Instance, &p) &&
            SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->ReadData, &p)) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_close_adc(const CloseAdcVar_t *in)
{
    uint16_t c, sid;
    struct SOMEIP_Transmit_Buffer *tb = req_begin(0x1702u, &c, &sid);
    if (!tb) return RT_INTERNAL_ERROR;
    if (!SOMEIP_Generator_Fill_UINT16(0, in->HandleAdc, &tb->payload[c], MAXP - c, &c))
        { SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_PARAMETER_NOT_VALID; }
    return req_finish(tb, c, sid);
}

ReturnCode_t rcp_open_pwm(const OpenPwmVar_t *in, OpenPwmReply_t *out)
{
    uint16_t c, sid, p = 0u, tag = 0u;
    struct SOMEIP_Transmit_Buffer *tb = req_begin(0x1800u, &c, &sid);
    ReturnCode_t rc;
    if (!tb) return RT_INTERNAL_ERROR;
    if (!(SOMEIP_Generator_Fill_UINT8(0, in->PinId, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_UINT32(1, in->IntervalTime, &tb->payload[c], MAXP - c, &c) &&
          SOMEIP_Generator_Fill_UINT32(2, in->DutyCycle, &tb->payload[c], MAXP - c, &c)))
        { SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_PARAMETER_NOT_VALID; }
    rc = req_finish(tb, c, sid);
    if (rc != RT_OK) return rc;
    return SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->HandlePwm, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_close_pwm(const ClosePwmVar_t *in)
{
    uint16_t c, sid;
    struct SOMEIP_Transmit_Buffer *tb = req_begin(0x1802u, &c, &sid);
    if (!tb) return RT_INTERNAL_ERROR;
    if (!SOMEIP_Generator_Fill_UINT16(0, in->HandlePwm, &tb->payload[c], MAXP - c, &c))
        { SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_PARAMETER_NOT_VALID; }
    return req_finish(tb, c, sid);
}
