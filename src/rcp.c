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
static uint16_t       s_chunk     = 1200u;   /* WriteImage chunk size (<=1200) */

static volatile bool                   s_done = false;
static volatile uint16_t               s_waitSid = 0u;  /* session currently awaited */
static volatile enum SOMEIP_ReturnCode s_rc   = SOMEIP_E_TIMEOUT;
static uint8_t  s_rx[MAXP];
static uint16_t s_rxLen = 0u;
static uint8_t  s_scratch[MAXP];             /* request-parameter scratch     */

/* --- asynchronous (non-blocking) request slots -------------------------- */
static struct {
    volatile uint16_t sid;                       /* 0 = free */
    rcp_async_cb      cb;
    void             *ctx;
    struct SOMEIP_Transmit_Buffer *tb;
    uint32_t          sentMs;
} s_async[RCP_ASYNC_MAX];
static uint32_t s_asyncTimeoutMs = 150u;

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
    /* Only the response/timeout of the request we are CURRENTLY waiting for may
     * complete the wait. The transmit layer shares this callback across all
     * pooled buffers, so a late response or 1 s timeout of an abandoned (retried)
     * buffer must not disturb the current request. */
    if (!pBuf || pBuf->waitForSessionId != s_waitSid) return;
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
    s_waitSid = sid;
    if (!SOMEIP_Transmit_Send(s_tr, tb)) {
        SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_SEND_ERROR;
    }
    start = SOMEIP_CB_GetTimeMS();
    while (!s_done && (SOMEIP_CB_GetTimeMS() - start) < s_timeoutMs) { rcp_poll(); NAP(); }
    if (!s_done) {
        /* Give up on this buffer: free its slot (ipV4Addr=0) and detach its
         * callback so a late response or the transmit layer's own timeout
         * cannot complete a later request's wait. */
        SOMEIP_CB_EnterCriticialSection();
        s_waitSid = 0u;
        tb->callback = NULL;
        tb->ipV4Addr[0] = 0u;
        SOMEIP_CB_LeaveCriticialSection();
        return RT_TIMEOUT;
    }
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

/* ======================== asynchronous requests ======================== *
 * Non-blocking: rcp_async_request() sends and returns. The reply (or a
 * timeout) is delivered later to the user callback - from the rx thread when a
 * response arrives, or from rcp_async_poll() when the deadline passes. The
 * synchronous path above is untouched; the two never share state.            */

static void on_async_response(struct SOMEIP_Transmit_Buffer *pBuf, bool ok,
                              enum SOMEIP_ReturnCode rc, const uint8_t *pRx, uint16_t rxLen)
{
    int i; rcp_async_cb cb = NULL; void *ctx = NULL;
    if (!pBuf) return;
    /* The transmit layer invokes this WHILE holding SOMEIP_CB critsec (a
     * NON-reentrant semaphore - someip-transmit.c ReceivedResponse/CheckTimers).
     * We must NOT take it again here (that self-deadlocks the rx thread). The
     * slot table is already serialized by the caller's held lock. */
    for (i = 0; i < RCP_ASYNC_MAX; ++i)
        if (s_async[i].sid != 0u && s_async[i].sid == pBuf->waitForSessionId) {
            cb = s_async[i].cb; ctx = s_async[i].ctx; s_async[i].sid = 0u; break;
        }
    if (cb) cb(ctx, ok ? (ReturnCode_t)rc : RT_TIMEOUT, ok ? pRx : NULL, ok ? rxLen : 0u);
}

ReturnCode_t rcp_async_request(uint16_t methodId, const uint8_t *params, uint16_t plen,
                               rcp_async_cb cb, void *ctx)
{
    struct SOMEIP_Transmit_Buffer *tb;
    struct SOMEIP_Header h;
    uint16_t consumed = 0u, sid;
    int slot = -1, i;
    if (s_sel >= s_epCount) return RT_INTERNAL_ERROR;
    SOMEIP_CB_EnterCriticialSection();
    for (i = 0; i < RCP_ASYNC_MAX; ++i) if (s_async[i].sid == 0u) { slot = i; break; }
    SOMEIP_CB_LeaveCriticialSection();
    if (slot < 0) return RT_INTERNAL_ERROR;          /* too many outstanding */
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
    tb->callback = on_async_response;
    tb->fireAndForget = false;
    tb->waitForSessionId = sid;
    tb->payloadLength = consumed;
    tb->udpPort = s_eps[s_sel].port;
    memcpy(tb->ipV4Addr, s_eps[s_sel].ip, 4);
    /* publish the slot before sending so a fast rx response is matched */
    SOMEIP_CB_EnterCriticialSection();
    s_async[slot].cb = cb; s_async[slot].ctx = ctx; s_async[slot].tb = tb;
    s_async[slot].sentMs = SOMEIP_CB_GetTimeMS(); s_async[slot].sid = sid;
    SOMEIP_CB_LeaveCriticialSection();
    if (!SOMEIP_Transmit_Send(s_tr, tb)) {
        SOMEIP_CB_EnterCriticialSection(); s_async[slot].sid = 0u; SOMEIP_CB_LeaveCriticialSection();
        SOMEIP_Transmit_ReleaseBufferOnError(s_tr, tb); return RT_SEND_ERROR;
    }
    return RT_OK;
}

void rcp_async_poll(void)
{
    int i;
    uint32_t now = SOMEIP_CB_GetTimeMS();
    SOMEIP_Transmit_CheckTimers();
    for (i = 0; i < RCP_ASYNC_MAX; ++i) {
        rcp_async_cb cb = NULL; void *ctx = NULL;
        SOMEIP_CB_EnterCriticialSection();
        if (s_async[i].sid != 0u && (now - s_async[i].sentMs) >= s_asyncTimeoutMs) {
            cb = s_async[i].cb; ctx = s_async[i].ctx;
            s_async[i].sid = 0u;   /* free the slot; the transmit buffer is reclaimed
                                    * by the transmit layer's own timeout or a late
                                    * reply (which finds no slot and is ignored). Do
                                    * NOT poke the buffer here - that can race the
                                    * transmit layer and wedge the poll. */
        }
        SOMEIP_CB_LeaveCriticialSection();
        if (cb) cb(ctx, RT_TIMEOUT, NULL, 0u);
    }
}

void rcp_set_async_timeout_ms(uint32_t ms) { s_asyncTimeoutMs = ms ? ms : 1u; }

/* ---- param builders / reply decoders for use with rcp_async_request ----- */
uint16_t rcp_enc_spi2(uint8_t *buf, uint16_t cap, uint16_t handle, uint32_t writeId,
                      const uint8_t *c0, uint16_t c0len, const uint8_t *c1, uint16_t c1len,
                      uint16_t r0len, uint16_t r1len)
{
    uint16_t pl = 0u;
    if (!(SOMEIP_Generator_Fill_UINT16(0, handle,  &buf[pl], (uint16_t)(cap - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT32(1, writeId, &buf[pl], (uint16_t)(cap - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT16(2, r0len,   &buf[pl], (uint16_t)(cap - pl), &pl) &&
          SOMEIP_Generator_Fill_BLOB(3, c0, c0len, &buf[pl], (uint16_t)(cap - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT16(4, r1len,   &buf[pl], (uint16_t)(cap - pl), &pl) &&
          SOMEIP_Generator_Fill_BLOB(5, c1, c1len, &buf[pl], (uint16_t)(cap - pl), &pl)))
        return 0u;
    return pl;
}

bool rcp_dec_spi2(const uint8_t *rx, uint16_t rxLen, uint8_t *rd0, uint16_t *l0, uint8_t *rd1, uint16_t *l1)
{
    uint16_t p = 0u, tag = 0u; uint32_t readId;
    return SOMEIP_Parser_Read_UINT32(&rx[p], rxLen - p, &tag, &readId, &p) &&
           SOMEIP_Parser_Read_BLOB(&rx[p], rxLen - p, &tag, rd0, l0, &p) &&
           SOMEIP_Parser_Read_BLOB(&rx[p], rxLen - p, &tag, rd1, l1, &p);
}

uint16_t rcp_enc_i2c_read(uint8_t *buf, uint16_t cap, uint16_t handle, uint16_t addr, uint32_t writeId,
                          const uint8_t *wr, uint16_t wrlen, uint16_t rdlen)
{
    uint16_t pl = 0u;
    if (!(SOMEIP_Generator_Fill_UINT16(0, handle,  &buf[pl], (uint16_t)(cap - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT16(1, addr,    &buf[pl], (uint16_t)(cap - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT16(2, rdlen,   &buf[pl], (uint16_t)(cap - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT32(3, writeId, &buf[pl], (uint16_t)(cap - pl), &pl) &&
          SOMEIP_Generator_Fill_BLOB(4, wr, wrlen, &buf[pl], (uint16_t)(cap - pl), &pl)))
        return 0u;
    return pl;
}

bool rcp_dec_i2c_read(const uint8_t *rx, uint16_t rxLen, uint8_t *rd, uint16_t *rdLen)
{
    uint16_t p = 0u, tag = 0u; uint32_t readId;
    return SOMEIP_Parser_Read_UINT32(&rx[p], rxLen - p, &tag, &readId, &p) &&
           SOMEIP_Parser_Read_BLOB(&rx[p], rxLen - p, &tag, rd, rdLen, &p);
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
void rcp_set_chunk(uint16_t n) { s_chunk = (n && n <= 1200u) ? n : 1200u; }

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

ReturnCode_t rcp_read_diagnosis_data(ReadDiagnosisDataReply_t *out)
{
    uint16_t p = 0u, tag = 0u;
    ReturnCode_t rc = rcp_xfer(0x1003u, NULL, 0);
    if (rc != RT_OK) return rc;
    out->Channel0Length = sizeof(out->Channel0);
    out->Channel1Length = sizeof(out->Channel1);
    out->Channel2Length = sizeof(out->Channel2);
    out->Channel3Length = sizeof(out->Channel3);
    return (SOMEIP_Parser_Read_BLOB(&s_rx[p], s_rxLen - p, &tag, out->Channel0, &out->Channel0Length, &p) &&
            SOMEIP_Parser_Read_BLOB(&s_rx[p], s_rxLen - p, &tag, out->Channel1, &out->Channel1Length, &p) &&
            SOMEIP_Parser_Read_BLOB(&s_rx[p], s_rxLen - p, &tag, out->Channel2, &out->Channel2Length, &p) &&
            SOMEIP_Parser_Read_BLOB(&s_rx[p], s_rxLen - p, &tag, out->Channel3, &out->Channel3Length, &p))
           ? RT_OK : RT_MALFORMED_MESSAGE;
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

/* Build a BOM-prefixed image name (UTF-8 BOM + name + NUL) into out[32].
 * Returns the length, or 0 if it does not fit. */
static uint16_t make_image_name(const char *name, uint8_t out[32])
{
    size_t L = strlen(name);
    if (L + 4u > 32u) return 0u;
    out[0] = 0xEFu; out[1] = 0xBBu; out[2] = 0xBFu;
    memcpy(&out[3], name, L); out[3 + L] = 0u;
    return (uint16_t)(L + 4u);
}

ReturnCode_t rcp_reboot(const char *name)
{
    uint8_t img[32]; uint16_t pl = 0u, n = make_image_name(name, img);
    if (!n) return RT_PARAMETER_NOT_VALID;
    if (!SOMEIP_Generator_Fill_BLOB(0, img, n, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl))
        return RT_PARAMETER_NOT_VALID;
    return rcp_xfer(0x1000u, s_scratch, pl);
}

ReturnCode_t rcp_start_update(const StartUpdateVar_t *in)
{
    uint16_t pl = 0u;
    if (!(SOMEIP_Generator_Fill_BLOB(0, in->ImageName, in->ImageNameLength, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_BLOB(1, in->InitVector, in->InitVectorLength, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl)))
        return RT_PARAMETER_NOT_VALID;
    return rcp_xfer(0x1004u, s_scratch, pl);
}

ReturnCode_t rcp_write_image(const WriteImageVar_t *in, WriteImageReply_t *out)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc;
    if (!(SOMEIP_Generator_Fill_BLOB(0, in->ImageName, in->ImageNameLength, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT32(1, in->WriteId, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_BLOB(2, in->WriteData, in->WriteDataLength, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl)))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1005u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    if (out) return SOMEIP_Parser_Read_UINT32(&s_rx[p], s_rxLen - p, &tag, &out->WriteId, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
    return RT_OK;
}

ReturnCode_t rcp_finish_update(const FinishUpdateVar_t *in)
{
    uint16_t pl = 0u;
    if (!(SOMEIP_Generator_Fill_BLOB(0, in->ImageName, in->ImageNameLength, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_BLOB(1, in->Signature, in->SignatureLength, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl)))
        return RT_PARAMETER_NOT_VALID;
    return rcp_xfer(0x1006u, s_scratch, pl);
}

ReturnCode_t rcp_flash_image(const char *name, const uint8_t *data, uint32_t dataLen,
                             const uint8_t *iv, uint16_t ivLen,
                             const uint8_t *sig, uint16_t sigLen, rcp_progress_cb cb)
{
    uint8_t nm[32]; uint16_t nmLen = make_image_name(name, nm);
    StartUpdateVar_t sv; FinishUpdateVar_t fv;
    ReturnCode_t rc; uint32_t pos = 0u, wid = 0u;
    if (!nmLen || ivLen > sizeof(sv.InitVector) || sigLen > sizeof(fv.Signature))
        return RT_PARAMETER_NOT_VALID;

    memset(&sv, 0, sizeof(sv));
    memcpy(sv.ImageName, nm, nmLen); sv.ImageNameLength = nmLen;
    memcpy(sv.InitVector, iv, ivLen); sv.InitVectorLength = ivLen;
    rc = rcp_start_update(&sv);
    if (rc != RT_OK) return rc;

    /* WriteImage acks in a few ms; on a lossy link a short timeout makes a
     * dropped chunk retry quickly (the buffer is abandoned cleanly). FinishUpdate
     * verifies the whole image and needs longer - bumped below. */
    rcp_set_timeout_ms(400);

    while (pos < dataLen) {
        WriteImageVar_t wv; WriteImageReply_t wr;
        uint16_t n = (uint16_t)((dataLen - pos > s_chunk) ? s_chunk : (dataLen - pos));
        memset(&wv, 0, sizeof(wv));
        memcpy(wv.ImageName, nm, nmLen); wv.ImageNameLength = nmLen;
        wv.WriteId = wid; wv.WriteDataLength = n; memcpy(wv.WriteData, data + pos, n);
        memset(&wr, 0, sizeof(wr));
        rc = rcp_write_image(&wv, &wr);
        if (rc != RT_OK) return rc;
        pos += n; wid++;
        if (cb) cb(pos, dataLen);
    }

    memset(&fv, 0, sizeof(fv));
    memcpy(fv.ImageName, nm, nmLen); fv.ImageNameLength = nmLen;
    memcpy(fv.Signature, sig, sigLen); fv.SignatureLength = sigLen;
    rcp_set_timeout_ms(3000);   /* signature verification over the whole image */
    return rcp_finish_update(&fv);
}

ReturnCode_t rcp_release_digital_pins(const ReleaseDigitalPinsVar_t *in)
{
    uint16_t pl = 0u;
    if (!SOMEIP_Generator_Fill_BLOB(0, in->PinIdList, in->PinIdListLength, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl))
        return RT_PARAMETER_NOT_VALID;
    return rcp_xfer(0x1105u, s_scratch, pl);
}

ReturnCode_t rcp_open_gpio(const OpenGpioVar_t *in, OpenGpioReply_t *out)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc;
    if (!(SOMEIP_Generator_Fill_UINT8(0, in->PinIdGpio, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT8(1, in->Direction, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl)))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1300u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    return SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->HandleGpio, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_set_gpio(const SetGpioVar_t *in)
{
    uint16_t pl = 0u;
    if (!SOMEIP_Generator_Fill_BLOB(0, in->GpioValues, in->GpioValuesLength, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl))
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
    if (!(SOMEIP_Generator_Fill_UINT8(0, in->PinIdSda, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT8(1, in->PinIdScl, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT8(2, in->ClockSpeed, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl)))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1200u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    return SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->HandleI2C, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_write_and_read_i2c(const WriteAndReadI2CVar_t *in, ReadI2CReply_t *out)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc;
    if (!(SOMEIP_Generator_Fill_UINT16(0, in->HandleI2C, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT16(1, in->DeviceAddress, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT16(2, in->ReadDataLength, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT32(3, in->WriteId, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_BLOB(4, in->WriteData, in->WriteDataLength, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl)))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1208u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    out->ReadDataLength = sizeof(out->ReadData);
    return (SOMEIP_Parser_Read_UINT32(&s_rx[p], s_rxLen - p, &tag, &out->ReadId, &p) &&
            SOMEIP_Parser_Read_BLOB(&s_rx[p], s_rxLen - p, &tag, out->ReadData, &out->ReadDataLength, &p)) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_read_i2c(const ReadI2CVar_t *in, ReadI2CReply_t *out)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc;
    if (!(SOMEIP_Generator_Fill_UINT16(0, in->HandleI2C, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT16(1, in->DeviceAddress, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT16(2, in->ReadDataLength, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl)))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1220u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    out->ReadDataLength = sizeof(out->ReadData);
    return (SOMEIP_Parser_Read_UINT32(&s_rx[p], s_rxLen - p, &tag, &out->ReadId, &p) &&
            SOMEIP_Parser_Read_BLOB(&s_rx[p], s_rxLen - p, &tag, out->ReadData, &out->ReadDataLength, &p)) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_write_i2c(const WriteI2CVar_t *in)
{
    uint16_t pl = 0u;
    if (!(SOMEIP_Generator_Fill_UINT16(0, in->HandleI2C, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT16(1, in->DeviceAddress, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT32(2, in->WriteId, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_BLOB(3, in->WriteData, in->WriteDataLength, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl)))
        return RT_PARAMETER_NOT_VALID;
    return rcp_xfer(0x1204u, s_scratch, pl);
}

ReturnCode_t rcp_close_i2c(const CloseI2CVar_t *in)
{
    uint16_t pl = 0u;
    if (!SOMEIP_Generator_Fill_UINT16(0, in->HandleI2C, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl))
        return RT_PARAMETER_NOT_VALID;
    return rcp_xfer(0x1202u, s_scratch, pl);
}

ReturnCode_t rcp_open_spi(const OpenSpiVar_t *in, OpenSpiReply_t *out)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc;
    if (!(SOMEIP_Generator_Fill_UINT8(0, in->PinIdMiso, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT8(1, in->PinIdSck, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT8(2, in->PinIdCs, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT8(3, in->PinIdMosi, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT8(4, in->Mode, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT32(5, in->ClockSpeed, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl)))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1500u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    return SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->HandleSpi, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_write_and_read_spi(const WriteAndReadSpiVar_t *in, WriteAndReadSpiReply_t *out)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc;
    if (!(SOMEIP_Generator_Fill_UINT16(0, in->HandleSpi, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT16(1, in->ReadDataLength, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT32(2, in->WriteId, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_BLOB(3, in->WriteData, in->WriteDataLength, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl)))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1508u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    out->ReadDataLength = sizeof(out->ReadData);
    return (SOMEIP_Parser_Read_UINT32(&s_rx[p], s_rxLen - p, &tag, &out->ReadId, &p) &&
            SOMEIP_Parser_Read_BLOB(&s_rx[p], s_rxLen - p, &tag, out->ReadData, &out->ReadDataLength, &p)) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_write_and_read_spi2(uint16_t handle, uint32_t writeId,
                                     const uint8_t *cmd0, uint16_t cmd0Len, uint8_t *rd0, uint16_t *rd0Len,
                                     const uint8_t *cmd1, uint16_t cmd1Len, uint8_t *rd1, uint16_t *rd1Len)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc; uint32_t readId;
    if (!(SOMEIP_Generator_Fill_UINT16(0, handle, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT32(1, writeId, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT16(2, *rd0Len, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_BLOB(3, cmd0, cmd0Len, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT16(4, *rd1Len, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_BLOB(5, cmd1, cmd1Len, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl)))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1509u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    return (SOMEIP_Parser_Read_UINT32(&s_rx[p], s_rxLen - p, &tag, &readId, &p) &&
            SOMEIP_Parser_Read_BLOB(&s_rx[p], s_rxLen - p, &tag, rd0, rd0Len, &p) &&
            SOMEIP_Parser_Read_BLOB(&s_rx[p], s_rxLen - p, &tag, rd1, rd1Len, &p)) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_close_spi(const CloseSpiVar_t *in)
{
    uint16_t pl = 0u;
    if (!SOMEIP_Generator_Fill_UINT16(0, in->HandleSpi, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl))
        return RT_PARAMETER_NOT_VALID;
    return rcp_xfer(0x1502u, s_scratch, pl);
}

ReturnCode_t rcp_open_adc(const OpenAdcVar_t *in, OpenAdcReply_t *out)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc;
    if (!SOMEIP_Generator_Fill_UINT8(0, in->PinId, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1700u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    return SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->HandleAdc, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_read_adc(const ReadAdcVar_t *in, ReadAdcReply_t *out)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc;
    if (!(SOMEIP_Generator_Fill_UINT16(0, in->HandleAdc, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT8(1, in->ChannelSelecct, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT8(2, in->VoltageReference, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl)))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1720u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    return (SOMEIP_Parser_Read_UINT8(&s_rx[p], s_rxLen - p, &tag, &out->Instance, &p) &&
            SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->ReadData, &p)) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_close_adc(const CloseAdcVar_t *in)
{
    uint16_t pl = 0u;
    if (!SOMEIP_Generator_Fill_UINT16(0, in->HandleAdc, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl))
        return RT_PARAMETER_NOT_VALID;
    return rcp_xfer(0x1702u, s_scratch, pl);
}

ReturnCode_t rcp_open_pwm(const OpenPwmVar_t *in, OpenPwmReply_t *out)
{
    uint16_t pl = 0u, p = 0u, tag = 0u;
    ReturnCode_t rc;
    if (!(SOMEIP_Generator_Fill_UINT8(0, in->PinId, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT32(1, in->IntervalTime, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl) &&
          SOMEIP_Generator_Fill_UINT32(2, in->DutyCycle, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl)))
        return RT_PARAMETER_NOT_VALID;
    rc = rcp_xfer(0x1800u, s_scratch, pl);
    if (rc != RT_OK) return rc;
    return SOMEIP_Parser_Read_UINT16(&s_rx[p], s_rxLen - p, &tag, &out->HandlePwm, &p) ? RT_OK : RT_MALFORMED_MESSAGE;
}

ReturnCode_t rcp_close_pwm(const ClosePwmVar_t *in)
{
    uint16_t pl = 0u;
    if (!SOMEIP_Generator_Fill_UINT16(0, in->HandlePwm, &s_scratch[pl], (uint16_t)(MAXP - pl), &pl))
        return RT_PARAMETER_NOT_VALID;
    return rcp_xfer(0x1802u, s_scratch, pl);
}
