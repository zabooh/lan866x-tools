/*
 * proxled.c  -  Sensor->actuator mini-app: proximity drives the on-board LEDs.
 *               Non-blocking, over SOME/IP (RCP). Pure C.
 *
 * The "real embedded app" example: it reads the VCNL4200 proximity sensor over
 * I2C (async) and lights the three on-board LEDs (LD1..LD3) as a level meter -
 * the closer your hand, the more LEDs light up. It ties together an INPUT read
 * and an OUTPUT write in one non-blocking superloop, with NO video involved
 * (unlike lan866x-clickdemo). This is the loop shape a real device runs.
 *
 * Wire: VCNL4200 @ 0x51, SDA=PA08 SCL=PA09 (Click slot 3); LEDs on PA02/PA06/PA10
 * (LD1/LD2/LD3, see led_map.json / docs/LEDDEMO.md).
 *
 * Usage:
 *   lan866x-proxled --ip 192.168.0.54
 *   lan866x-proxled --max 600
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
#define NLED 3

static volatile sig_atomic_t g_run = 1;
static void on_sigint(int sig) { (void)sig; g_run = 0; }

static uint16_t s_i2c;
static uint32_t s_wid = 0;
static volatile int      s_pending = 0;
static volatile uint16_t s_prox = 0;

static const int     s_ledPin[NLED] = { 2, 6, 10 };   /* LD1, LD2, LD3 */
static uint16_t      s_ledHandle[NLED];

static void on_prox(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{
    (void)ctx;
    if (rc == RT_OK && rx) {
        uint8_t d[8] = {0}; uint16_t l = sizeof(d);
        if (rcp_dec_i2c_read(rx, rxLen, d, &l) && l >= 2) s_prox = (uint16_t)(d[0] | (d[1] << 8));
    }
    s_pending = 0;
}

static int cfg_write(uint8_t reg, uint8_t lo, uint8_t hi)
{
    WriteI2CVar_t w; memset(&w, 0, sizeof(w));
    w.HandleI2C = s_i2c; w.DeviceAddress = VCNL4200_ADDR; w.WriteId = s_wid++;
    w.WriteData[0] = reg; w.WriteData[1] = lo; w.WriteData[2] = hi; w.WriteDataLength = 3;
    return rcp_write_i2c(&w) == RT_OK;
}

/* set one LED (blocking; only called when the level actually changes) */
static void led_set(int idx, int on)
{
    SetGpioVar_t sv; memset(&sv, 0, sizeof(sv));
    sv.GpioValues[0] = (uint8_t)(s_ledHandle[idx] >> 8);
    sv.GpioValues[1] = (uint8_t)(s_ledHandle[idx] & 0xFF);
    sv.GpioValues[2] = (uint8_t)(on ? 1 : 0);
    sv.GpioValuesLength = 3;
    rcp_set_gpio(&sv);
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, sda = 8, scl = 9, maxRaw = 400, level = -1;
    ReleaseDigitalPinsVar_t rel; OpenI2CVar_t ov; OpenI2CReply_t orep;
    WriteAndReadI2CVar_t idrq; ReadI2CReply_t idrp; uint16_t id = 0;
    DWORD nextRead;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-proxled - proximity drives the on-board LEDs (level meter), non-blocking (pure C)\n"
                   "  --max <n>   raw value that lights all 3 LEDs (default 400)\n"
                   "  --sda <n> --scl <n>   I2C pins (default 8 / 9)\n"
                   "  --ip/--ep   target endpoint\n"
                   "Closer hand = more LEDs. Ctrl+C to stop (LEDs off).\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")  && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")    && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--max")   && i+1<argc) maxRaw = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sda")   && i+1<argc) sda = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scl")   && i+1<argc) scl = atoi(argv[++i]);
    }
    if (maxRaw < 1) maxRaw = 1;

    if (tool_select(wantIp, wantEp, 5, "LAN866x proximity->LED app (non-blocking, pure C)") < 0) return 2;

    /* --- one-time blocking setup: I2C + VCNL4200, then the 3 LED GPIOs --- */
    memset(&rel, 0, sizeof(rel));
    rel.PinIdList[0] = (uint8_t)sda; rel.PinIdList[1] = (uint8_t)scl; rel.PinIdListLength = 2;
    rcp_release_digital_pins(&rel); Sleep(25);
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdSda = (uint8_t)sda; ov.PinIdScl = (uint8_t)scl; ov.ClockSpeed = 1;
    if (rcp_open_i2c(&ov, &orep) != RT_OK) { printf("OpenI2C failed.\n"); return 3; }
    s_i2c = orep.HandleI2C;

    memset(&idrq, 0, sizeof(idrq)); memset(&idrp, 0, sizeof(idrp));
    idrq.HandleI2C = s_i2c; idrq.DeviceAddress = VCNL4200_ADDR; idrq.ReadDataLength = 2;
    idrq.WriteId = s_wid++; idrq.WriteDataLength = 1; idrq.WriteData[0] = VCNL4200_ID_REG;
    if (rcp_write_and_read_i2c(&idrq, &idrp) == RT_OK && idrp.ReadDataLength >= 2)
        id = (uint16_t)(idrp.ReadData[0] | (idrp.ReadData[1] << 8));
    if (id != VCNL4200_ID_VALUE) { printf("VCNL4200 not found (ID=0x%04X).\n", id); return 4; }
    Sleep(25); cfg_write(VCNL4200_PS_CONF1, 0x02, 0x08);
    Sleep(25); cfg_write(VCNL4200_PS_CONF3, 0x01, 0x07);
    Sleep(25); cfg_write(VCNL4200_ALS_CONF, 0x00, 0x00);
    Sleep(25);

    for (i = 0; i < NLED; ++i) {
        OpenGpioVar_t gv; OpenGpioReply_t grep;
        ReleaseDigitalPinsVar_t r; memset(&r, 0, sizeof(r));
        r.PinIdList[0] = (uint8_t)s_ledPin[i]; r.PinIdListLength = 1;
        rcp_release_digital_pins(&r); Sleep(15);
        memset(&gv, 0, sizeof(gv)); memset(&grep, 0, sizeof(grep));
        gv.PinIdGpio = (uint8_t)s_ledPin[i]; gv.Direction = 1;
        if (rcp_open_gpio(&gv, &grep) != RT_OK) { printf("OpenGpio failed on PA%02d.\n", s_ledPin[i]); return 5; }
        s_ledHandle[i] = grep.HandleGpio; Sleep(15);
    }

    rcp_set_async_timeout_ms(150);
    signal(SIGINT, on_sigint);
    printf("\nProximity -> LED level meter. Move your hand toward the sensor.\n"
           "  0 LEDs (far) .. 3 LEDs (near). Ctrl+C to stop.\n\n");

    nextRead = GetTickCount();
    while (g_run) {
        /* INPUT: async proximity read whenever none is in flight */
        if (!s_pending) {
            uint8_t params[64], reg = VCNL4200_PS_DATA; uint16_t n;
            n = rcp_enc_i2c_read(params, sizeof(params), s_i2c, VCNL4200_ADDR, s_wid++, &reg, 1, 2);
            if (n && rcp_async_request(0x1208u, params, n, on_prox, NULL) == RT_OK) s_pending = 1;
        }
        rcp_async_poll();

        /* DECIDE + OUTPUT: map proximity to a 0..3 level; write LEDs only on change */
        {
            int v = s_prox, lvl = v * (NLED + 1) / maxRaw;   /* 0..NLED */
            if (lvl > NLED) lvl = NLED; if (lvl < 0) lvl = 0;
            if (lvl != level) {
                for (i = 0; i < NLED; ++i) led_set(i, i < lvl);
                level = lvl;
                printf("\r  raw=%5u  level=%d/%d  [%c%c%c]   ", s_prox, lvl, NLED,
                       lvl > 0 ? '#' : '-', lvl > 1 ? '#' : '-', lvl > 2 ? '#' : '-');
                fflush(stdout);
            }
        }
        Sleep(5);
        (void)nextRead;
    }

    /* clean exit: LEDs off, close I2C */
    for (i = 0; i < NLED; ++i) led_set(i, 0);
    { CloseI2CVar_t c; c.HandleI2C = s_i2c; rcp_close_i2c(&c); }
    printf("\nStopped. LEDs off.\n");
    return 0;
}
