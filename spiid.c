/*
 * spiid.c  -  Identify the Thumbstick over SPI, non-blocking, via SOME/IP (RCP).
 *             Pure C.
 *
 * The Thumbstick Click reads its 2-axis joystick through an MCP3204 12-bit ADC.
 * Unlike the VCNL4200 (which has an ID register, see lan866x-i2cid), the MCP3204
 * has NO silicon ID register. So "identifying" it over SPI means proving the
 * device responds with a valid 12-bit conversion on its channels: a centred
 * joystick reads ~2048 on both axes at rest. That round-trip IS the fingerprint.
 *
 * The SPI transfer is issued with the ASYNC RCP API (rcp_async_request /
 * rcp_async_poll) so the main loop never blocks while the read is in flight.
 *
 * Wire: MISO=PA12 SCK=PA13 CS=PA14 MOSI=PA15, SPI mode 1, ~1.92 MHz (the board
 * DIP default for Click slot 4). MCP3204 single-ended read = 3-byte full-duplex
 * transfer; the 12-bit result is in bytes [1..2] of the reply.
 *
 * Usage:
 *   lan866x-spiid                          read thumbstick axes (ch1=X, ch0=Y)
 *   lan866x-spiid --ip 192.168.0.54
 *   lan866x-spiid --miso 12 --sck 13 --cs 14 --mosi 15 --mode 1 --speed 1923000
 */
#include <stdlib.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

#define ADC_MAX 0x0FFFu   /* MCP3204 is 12-bit */

/* Reply state, written by the async callback (single-thread: no lock needed). */
static volatile int s_done = 0, s_ok = 0;
static uint16_t     s_val = 0;

/* Runs inline from rcp_async_poll(). Keep short; no rcp_* calls in here. */
static void on_spi(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{
    (void)ctx;
    s_done = 1;
    if (rc == RT_OK && rx) {
        uint8_t d[8] = {0}; uint16_t l = sizeof(d);
        if (rcp_dec_spi1(rx, rxLen, d, &l) && l >= 3) {
            s_val = (uint16_t)(((d[1] << 8) | d[2]) & ADC_MAX);  /* 12-bit result */
            s_ok = 1;
        }
    }
}

/* Read one MCP3204 single-ended channel via a non-blocking SPI transfer.
 * Returns 1 and the 12-bit value on success. */
static int read_channel(uint16_t handle, uint32_t *wid, int ch, uint16_t *out, unsigned long *spins)
{
    int attempt;
    for (attempt = 0; attempt < 5; ++attempt) {
        uint8_t tx[3], params[64]; uint16_t n;
        tx[0] = 0x06;                       /* start + SGL/DIFF (single-ended)       */
        tx[1] = (uint8_t)(ch << 6);         /* channel select in the high bits        */
        tx[2] = 0xFF;                       /* don't-care while the result clocks out */
        s_done = 0; s_ok = 0;
        n = rcp_enc_spi1(params, sizeof(params), handle, (*wid)++, tx, 3, 3);
        if (!n) return 0;
        if (rcp_async_request(0x1508u, params, n, on_spi, NULL) != RT_OK) { Sleep(20); continue; }
        while (!s_done) { rcp_async_poll(); Sleep(2); (*spins)++; }   /* <-- loop stays free */
        if (s_ok) { *out = s_val; return 1; }
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, miso = 12, sck = 13, cs = 14, mosi = 15, mode = 1;
    long speed = 1923000;
    ReleaseDigitalPinsVar_t rel; OpenSpiVar_t ov; OpenSpiReply_t orep;
    uint16_t handle; uint32_t wid = 0; unsigned long spins = 0;
    uint16_t x = 0, y = 0; int okX, okY, p = 0;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-spiid - identify the Thumbstick (MCP3204) over SPI, non-blocking (pure C)\n"
                   "  --miso <n> --sck <n> --cs <n> --mosi <n>   SPI pins (default 12/13/14/15)\n"
                   "  --mode <0..3>  SPI mode (default 1)\n"
                   "  --speed <hz>   clock (default 1923000)\n"
                   "  --ip/--ep      target endpoint\n"
                   "Note: the MCP3204 has no ID register; a valid 12-bit axis read is its fingerprint.\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")    && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")      && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--miso")    && i+1<argc) miso = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sck")     && i+1<argc) sck  = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cs")      && i+1<argc) cs   = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mosi")    && i+1<argc) mosi = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mode")    && i+1<argc) mode = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--speed")   && i+1<argc) speed = strtol(argv[++i], NULL, 0);
    }

    if (tool_select(wantIp, wantEp, 5, "LAN866x SPI thumbstick read (non-blocking, pure C)") < 0) return 2;

    /* One-time blocking setup: release the 4 pins, then OpenSpi. */
    memset(&rel, 0, sizeof(rel));
    rel.PinIdList[p++] = (uint8_t)miso; rel.PinIdList[p++] = (uint8_t)sck;
    rel.PinIdList[p++] = (uint8_t)cs;   rel.PinIdList[p++] = (uint8_t)mosi;
    rel.PinIdListLength = (uint8_t)p;
    rcp_release_digital_pins(&rel);
    Sleep(25);
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdMiso = (uint8_t)miso; ov.PinIdSck = (uint8_t)sck; ov.PinIdCs = (uint8_t)cs;
    ov.PinIdMosi = (uint8_t)mosi; ov.Mode = (uint8_t)mode; ov.ClockSpeed = (uint32_t)speed;
    if (rcp_open_spi(&ov, &orep) != RT_OK) { printf("OpenSpi failed (SPI not configured?).\n"); return 3; }
    handle = orep.HandleSpi;

    rcp_set_async_timeout_ms(300);

    printf("\nReading Thumbstick MCP3204 over SPI (MISO=PA%02d SCK=PA%02d CS=PA%02d MOSI=PA%02d, non-blocking)...\n",
           miso, sck, cs, mosi);

    okX = read_channel(handle, &wid, 1, &x, &spins);   /* ch1 = X axis */
    okY = read_channel(handle, &wid, 0, &y, &spins);   /* ch0 = Y axis */

    if (okX && okY) {
        printf("  channel 1 (X axis): raw=%4u (0x%03X)\n", x, x);
        printf("  channel 0 (Y axis): raw=%4u (0x%03X)\n", y, y);
        printf("  (loop spun %lu times while the reads were in flight)\n", spins);
        printf("  -> MCP3204 Thumbstick responding. (No silicon ID register exists;\n"
               "     a valid 12-bit conversion - ~2048 per axis at rest - is its fingerprint.)\n");
    } else {
        printf("  read failed (X=%s Y=%s). Is the Thumbstick in slot 4 and SPI routed?\n",
               okX ? "ok" : "FAIL", okY ? "ok" : "FAIL");
    }

    { CloseSpiVar_t c; c.HandleSpi = handle; rcp_close_spi(&c); }
    return (okX && okY) ? 0 : 4;
}
