/*
 * i2cid.c  -  Read a device ID over I2C, non-blocking, via SOME/IP (RCP). Pure C.
 *
 * Example: identify the Proximity 3 Click (VCNL4200) on the demo board by reading
 * its ID register (0x0E) and checking it equals 0x1058. The I2C read is issued
 * with the ASYNC RCP API (rcp_async_request/rcp_async_poll) so the main loop never
 * blocks on the round-trip - it spins freely while the read is in flight.
 *
 * Wire: VCNL4200 @ 0x51, SDA=PA08 SCL=PA09, 400 kHz (the board DIP default for
 * Click slot 3). The ID register is 16-bit, little-endian (LSB first).
 *
 * Usage:
 *   lan866x-i2cid                          read VCNL4200 ID @0x51 reg 0x0E
 *   lan866x-i2cid --ip 192.168.0.54
 *   lan866x-i2cid --addr 0x51 --reg 0x0E --sda 8 --scl 9 --speed 1
 */
#include <stdlib.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

#define VCNL4200_ID_VALUE 0x1058u    /* expected ID of the Proximity 3 Click */

/* Reply state, written by the async callback (single-thread: no lock needed). */
static volatile int s_done = 0, s_ok = 0;
static uint16_t     s_id = 0;
static ReturnCode_t s_rc = RT_TIMEOUT;

/* Runs inline from rcp_async_poll(). Keep short; no rcp_* calls in here. */
static void on_id(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{
    (void)ctx;
    s_rc = rc; s_done = 1;
    if (rc == RT_OK && rx) {
        uint8_t d[8] = {0}; uint16_t l = sizeof(d);
        if (rcp_dec_i2c_read(rx, rxLen, d, &l) && l >= 2) {
            s_id = (uint16_t)(d[0] | (d[1] << 8));   /* VCNL4200 = LSB first */
            s_ok = 1;
        }
    }
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, sda = 8, scl = 9, speed = 1, attempt;
    long addr = 0x51, reg = 0x0E;
    ReleaseDigitalPinsVar_t rel; OpenI2CVar_t ov; OpenI2CReply_t orep;
    uint16_t handle; uint32_t wid = 0; unsigned long spins = 0;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-i2cid - read a device ID over I2C, non-blocking (pure C)\n"
                   "  --addr <a>      I2C device address (default 0x51 = VCNL4200)\n"
                   "  --reg  <r>      ID register (default 0x0E)\n"
                   "  --sda <n> --scl <n>   I2C pins (PA index; default 8 / 9)\n"
                   "  --speed <0|1>  0=100kHz, 1=400kHz (default 1)\n"
                   "  --ip/--ep      target endpoint\n"
                   "(hex accepted, e.g. --addr 0x51 --reg 0x0E)\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")    && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")      && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--addr")    && i+1<argc) addr = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--reg")     && i+1<argc) reg  = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--sda")     && i+1<argc) sda  = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scl")     && i+1<argc) scl  = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--speed")   && i+1<argc) speed = atoi(argv[++i]);
    }

    if (tool_select(wantIp, wantEp, 5, "LAN866x I2C ID read (non-blocking, pure C)") < 0) return 2;

    /* One-time blocking setup: release the pins, then OpenI2C. */
    memset(&rel, 0, sizeof(rel));
    rel.PinIdList[0] = (uint8_t)sda; rel.PinIdList[1] = (uint8_t)scl; rel.PinIdListLength = 2;
    rcp_release_digital_pins(&rel);
    Sleep(25);
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdSda = (uint8_t)sda; ov.PinIdScl = (uint8_t)scl; ov.ClockSpeed = (uint8_t)speed;
    if (rcp_open_i2c(&ov, &orep) != RT_OK) { printf("OpenI2C failed (I2C not configured?).\n"); return 3; }
    handle = orep.HandleI2C;

    rcp_set_async_timeout_ms(300);   /* deadline per attempt */

    printf("\nReading ID of 0x%02lX register 0x%02lX (SDA=PA%02d SCL=PA%02d, non-blocking)...\n",
           addr, reg, sda, scl);

    /* Fire the async read; spin the loop (never blocking on the network) until the
     * callback delivers the reply or the deadline elapses. Retry a few times. */
    for (attempt = 0; attempt < 5 && !s_ok; ++attempt) {
        uint8_t params[64], regb = (uint8_t)reg; uint16_t n;
        s_done = 0; s_ok = 0;
        n = rcp_enc_i2c_read(params, sizeof(params), handle, (uint16_t)addr, wid++, &regb, 1, 2);
        if (!n) { printf("param encode failed.\n"); break; }
        if (rcp_async_request(0x1208u, params, n, on_id, NULL) != RT_OK) { Sleep(20); continue; }
        while (!s_done) { rcp_async_poll(); Sleep(2); spins++; }   /* <-- loop stays free */
    }

    if (s_ok) {
        printf("  ID = 0x%04X   (loop spun %lu times while the read was in flight)\n", s_id, spins);
        if (addr == 0x51 && (long)reg == 0x0E)
            printf("  -> %s\n", s_id == VCNL4200_ID_VALUE
                   ? "VCNL4200 (Proximity 3 Click) detected." : "unexpected ID for a VCNL4200.");
    } else {
        printf("  read failed (rc=%d). Is the device present / I2C routed to slot 3?\n", s_rc);
    }

    { CloseI2CVar_t c; c.HandleI2C = handle; rcp_close_i2c(&c); }
    return s_ok ? 0 : 4;
}
