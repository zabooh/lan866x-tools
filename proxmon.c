/*
 * proxmon.c  -  Live proximity monitor over I2C, non-blocking, via SOME/IP (RCP).
 *               Pure C.
 *
 * The natural sequel to lan866x-i2cid: where i2cid reads the VCNL4200 *ID* once,
 * this reads its *proximity data* (PS_DATA) continuously and draws a live bar.
 * Each read is issued with the ASYNC RCP API and the render loop runs at a steady
 * cadence regardless of when replies arrive - the proper "read a sensor in a
 * superloop without stalling" pattern.
 *
 * Wire: VCNL4200 @ 0x51, SDA=PA08 SCL=PA09, 400 kHz (Click slot 3 default).
 *
 * Usage:
 *   lan866x-proxmon --ip 192.168.0.54
 *   lan866x-proxmon --max 600 --hz 20
 */
#include <stdlib.h>
#include <signal.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

#define VCNL4200_ADDR     0x51u
#define VCNL4200_ID_REG   0x0Eu
#define VCNL4200_ID_VALUE 0x1058u
#define VCNL4200_PS_CONF1 0x03u
#define VCNL4200_PS_CONF3 0x04u
#define VCNL4200_ALS_CONF 0x00u
#define VCNL4200_PS_DATA  0x08u

static volatile sig_atomic_t g_run = 1;
static void on_sigint(int sig) { (void)sig; g_run = 0; }

static uint16_t s_handle;
static uint32_t s_wid = 0;
static volatile int      s_pending = 0;
static volatile uint16_t s_prox = 0;
static volatile int      s_ok = 0;

/* async reply: decode PS_DATA (2 bytes, LSB first), clear the pending flag */
static void on_prox(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{
    (void)ctx;
    if (rc == RT_OK && rx) {
        uint8_t d[8] = {0}; uint16_t l = sizeof(d);
        if (rcp_dec_i2c_read(rx, rxLen, d, &l) && l >= 2) { s_prox = (uint16_t)(d[0] | (d[1] << 8)); s_ok = 1; }
        else s_ok = 0;
    } else s_ok = 0;
    s_pending = 0;
}

/* one-time blocking I2C write of [reg, lo, hi] */
static int cfg_write(uint8_t reg, uint8_t lo, uint8_t hi)
{
    WriteI2CVar_t w; memset(&w, 0, sizeof(w));
    w.HandleI2C = s_handle; w.DeviceAddress = VCNL4200_ADDR; w.WriteId = s_wid++;
    w.WriteData[0] = reg; w.WriteData[1] = lo; w.WriteData[2] = hi; w.WriteDataLength = 3;
    return rcp_write_i2c(&w) == RT_OK;
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, sda = 8, scl = 9, maxRaw = 400, hz = 15;
    ReleaseDigitalPinsVar_t rel; OpenI2CVar_t ov; OpenI2CReply_t orep;
    WriteAndReadI2CVar_t idrq; ReadI2CReply_t idrp; uint16_t id = 0;
    DWORD nextRender;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-proxmon - live VCNL4200 proximity bar over I2C, non-blocking (pure C)\n"
                   "  --max <n>   raw value that fills the bar (default 400)\n"
                   "  --hz <n>    render rate (default 15)\n"
                   "  --sda <n> --scl <n>   I2C pins (default 8 / 9)\n"
                   "  --ip/--ep   target endpoint\n"
                   "Ctrl+C to stop.\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")  && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")    && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--max")   && i+1<argc) maxRaw = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--hz")    && i+1<argc) hz = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sda")   && i+1<argc) sda = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scl")   && i+1<argc) scl = atoi(argv[++i]);
    }
    if (maxRaw < 1) maxRaw = 1;
    if (hz < 1) hz = 1; if (hz > 100) hz = 100;

    if (tool_select(wantIp, wantEp, 5, "LAN866x proximity monitor (non-blocking, pure C)") < 0) return 2;

    /* one-time blocking setup: OpenI2C, verify ID, enable the PS engine */
    memset(&rel, 0, sizeof(rel));
    rel.PinIdList[0] = (uint8_t)sda; rel.PinIdList[1] = (uint8_t)scl; rel.PinIdListLength = 2;
    rcp_release_digital_pins(&rel); Sleep(25);
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdSda = (uint8_t)sda; ov.PinIdScl = (uint8_t)scl; ov.ClockSpeed = 1; /* 400 kHz */
    if (rcp_open_i2c(&ov, &orep) != RT_OK) { printf("OpenI2C failed.\n"); return 3; }
    s_handle = orep.HandleI2C;

    memset(&idrq, 0, sizeof(idrq)); memset(&idrp, 0, sizeof(idrp));
    idrq.HandleI2C = s_handle; idrq.DeviceAddress = VCNL4200_ADDR; idrq.ReadDataLength = 2;
    idrq.WriteId = s_wid++; idrq.WriteDataLength = 1; idrq.WriteData[0] = VCNL4200_ID_REG;
    if (rcp_write_and_read_i2c(&idrq, &idrp) == RT_OK && idrp.ReadDataLength >= 2)
        id = (uint16_t)(idrp.ReadData[0] | (idrp.ReadData[1] << 8));
    if (id != VCNL4200_ID_VALUE) { printf("VCNL4200 not found (ID=0x%04X).\n", id); return 4; }

    Sleep(25); cfg_write(VCNL4200_PS_CONF1, 0x02, 0x08);   /* IT 1.5T | duty 1/160 ; 16-bit */
    Sleep(25); cfg_write(VCNL4200_PS_CONF3, 0x01, 0x07);   /* sunlight cancel ; LED 200 mA */
    Sleep(25); cfg_write(VCNL4200_ALS_CONF, 0x00, 0x00);
    Sleep(25);

    rcp_set_async_timeout_ms(150);
    signal(SIGINT, on_sigint);
    printf("\nLive proximity (VCNL4200). Move your hand near the sensor. Ctrl+C to stop.\n\n");

    nextRender = GetTickCount();
    while (g_run) {
        /* fire a read whenever none is in flight (non-blocking) */
        if (!s_pending) {
            uint8_t params[64], reg = VCNL4200_PS_DATA; uint16_t n;
            n = rcp_enc_i2c_read(params, sizeof(params), s_handle, VCNL4200_ADDR, s_wid++, &reg, 1, 2);
            if (n && rcp_async_request(0x1208u, params, n, on_prox, NULL) == RT_OK) s_pending = 1;
        }
        rcp_async_poll();   /* deliver replies / timeouts; callback runs from here */

        /* render the bar at a steady cadence, decoupled from reply timing */
        {
            DWORD now = GetTickCount();
            if ((long)(now - nextRender) >= 0) {
                int W = 40, k, v = s_prox;
                int fill = v * W / maxRaw; if (fill > W) fill = W; if (fill < 0) fill = 0;
                printf("\r  [");
                for (k = 0; k < W; ++k) putchar(k < fill ? '#' : '-');
                printf("] raw=%5u %s ", s_prox, s_ok ? "  " : "..");
                fflush(stdout);
                nextRender += (DWORD)(1000 / hz);
            }
        }
        Sleep(2);
    }

    { CloseI2CVar_t c; c.HandleI2C = s_handle; rcp_close_i2c(&c); }
    printf("\nStopped.\n");
    return 0;
}
