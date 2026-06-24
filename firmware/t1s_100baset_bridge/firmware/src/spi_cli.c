/*
 * spi_cli.c - SPI + analog family of the bridge CLI: mirrors the host tools
 *   spi, spiid, thumbmon, adc, pwm. Registered as the "spi" SYS_CMD group; type
 *   the name directly. thumbmon is bounded ([secs]) + Ctrl-C/'q'. Default SPI
 *   pins MISO=PA12/SCK=PA13/CS=PA14/MOSI=PA15 (Click slot-4), like clickdemo. No C++.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "definitions.h"
#include "system/command/sys_command.h"
#include "config/default/system/console/sys_console.h"
#include "rcp.h"
#include "plat.h"
#include "lan866x_cli.h"

#define SPI_MISO 12u
#define SPI_SCK  13u
#define SPI_CS   14u
#define SPI_MOSI 15u
#define ADC_MAX  0x0FFFu

static int chk_abort(SYS_CONSOLE_HANDLE con)
{
    char ch; int hit = 0;
    while (SYS_CONSOLE_Read(con, &ch, 1) > 0)
        if (ch == 0x03 || ch == 'q' || ch == 'Q') hit = 1;
    return hit;
}

static int hex2bytes(const char *s, uint8_t *out, int maxlen)
{
    int n = 0;
    for (; s[0] && s[1] && n < maxlen; s += 2) {
        char b[3] = { s[0], s[1], 0 }; char *e = NULL;
        long v = strtol(b, &e, 16);
        if (*e) return -1;
        out[n++] = (uint8_t)v;
    }
    return s[0] ? -1 : n;
}

static int spi_open(uint8_t miso, uint8_t sck, uint8_t cs, uint8_t mosi,
                    uint8_t mode, uint32_t hz, uint16_t *h)
{
    ReleaseDigitalPinsVar_t rel; OpenSpiVar_t ov; OpenSpiReply_t orep; uint16_t p = 0;
    uint8_t pins[4] = { miso, sck, cs, mosi }; int i;
    memset(&rel, 0, sizeof(rel));
    for (i = 0; i < 4; ++i) if (pins[i] != 0xFFu) rel.PinIdList[p++] = pins[i];
    rel.PinIdListLength = p; rcp_release_digital_pins(&rel); plat_sleep_ms(25);
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdMiso = miso; ov.PinIdSck = sck; ov.PinIdCs = cs; ov.PinIdMosi = mosi;
    ov.Mode = mode; ov.ClockSpeed = hz;
    if (rcp_open_spi(&ov, &orep) != RT_OK) return 0;
    *h = orep.HandleSpi; return 1;
}
static void spi_close(uint16_t h) { CloseSpiVar_t c; c.HandleSpi = h; rcp_close_spi(&c); }

/* --- spi <txhex> [mode] [speedHz] (mirrors spi.c) -------------------------- */
static void cmd_spi(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    uint8_t tx[256]; int txLen, i; uint8_t mode = 0u; uint32_t hz = 1000000u; uint16_t h;
    WriteAndReadSpiVar_t wr; WriteAndReadSpiReply_t rr;
    (void)pCmdIO;
    if (argc < 2) { SYS_CONSOLE_PRINT("Usage: spi <txhex> [mode 0..3] [speedHz]  (pins 12/13/14/15)\r\n"); return; }
    txLen = hex2bytes(argv[1], tx, sizeof(tx));
    if (txLen <= 0) { SYS_CONSOLE_PRINT("invalid hex (even digits)\r\n"); return; }
    if (argc >= 3) mode = (uint8_t)strtoul(argv[2], NULL, 10);
    if (argc >= 4) hz   = (uint32_t)strtoul(argv[3], NULL, 10);

    if (!sel_first_ep()) { SYS_CONSOLE_PRINT("[spi] no endpoint - run 'discovery' first\r\n"); return; }
    rcp_set_timeout_ms(800); rcp_set_retries(2);
    if (!spi_open(SPI_MISO, SPI_SCK, SPI_CS, SPI_MOSI, mode, hz, &h)) { SYS_CONSOLE_PRINT("OpenSpi failed\r\n"); goto restore; }

    memset(&wr, 0, sizeof(wr)); memset(&rr, 0, sizeof(rr));
    wr.HandleSpi = h; wr.ReadDataLength = (uint16_t)txLen; wr.WriteId = 0;
    wr.WriteDataLength = (uint16_t)txLen; memcpy(wr.WriteData, tx, txLen);
    if (rcp_write_and_read_spi(&wr, &rr) == RT_OK) {
        SYS_CONSOLE_PRINT("TX:"); for (i = 0; i < txLen; ++i) SYS_CONSOLE_PRINT(" %02X", tx[i]);
        SYS_CONSOLE_PRINT("\r\nRX:"); for (i = 0; i < (int)rr.ReadDataLength; ++i) SYS_CONSOLE_PRINT(" %02X", rr.ReadData[i]);
        SYS_CONSOLE_PRINT("\r\n");
    } else SYS_CONSOLE_PRINT("WriteAndReadSpi failed\r\n");
    spi_close(h);
restore:
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

/* --- shared single-channel MCP3204 read (async, like spiid.c) -------------- */
static volatile int s_sp_done, s_sp_ok; static uint16_t s_sp_val;
static void on_sp1(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{
    (void)ctx; s_sp_done = 1;
    if (rc == RT_OK && rx) { uint8_t d[8] = {0}; uint16_t l = sizeof(d);
        if (rcp_dec_spi1(rx, rxLen, d, &l) && l >= 3) { s_sp_val = (uint16_t)(((d[1] << 8) | d[2]) & ADC_MAX); s_sp_ok = 1; } }
}
static int mcp3204_read(uint16_t h, uint32_t *wid, int ch, uint16_t *out)
{
    int attempt;
    for (attempt = 0; attempt < 5; ++attempt) {
        uint8_t tx[3], pp[64]; uint16_t n;
        tx[0] = 0x06; tx[1] = (uint8_t)(ch << 6); tx[2] = 0xFF;
        s_sp_done = 0; s_sp_ok = 0;
        n = rcp_enc_spi1(pp, sizeof(pp), h, (*wid)++, tx, 3, 3);
        if (!n) return 0;
        if (rcp_async_request(0x1508u, pp, n, on_sp1, NULL) != RT_OK) { plat_sleep_ms(20); continue; }
        while (!s_sp_done) { rcp_async_poll(); plat_sleep_ms(2); }
        if (s_sp_ok) { *out = s_sp_val; return 1; }
    }
    return 0;
}

/* --- spiid : identify the Thumbstick (MCP3204) over SPI (spiid.c) ----------- */
static void cmd_spiid(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    uint16_t h, x = 0, y = 0; uint32_t wid = 0u; int okX, okY;
    (void)pCmdIO; (void)argc; (void)argv;
    if (!sel_first_ep()) { SYS_CONSOLE_PRINT("[spiid] no endpoint - run 'discovery' first\r\n"); return; }
    rcp_set_timeout_ms(800); rcp_set_retries(2);
    if (!spi_open(SPI_MISO, SPI_SCK, SPI_CS, SPI_MOSI, 1u, 1923000u, &h)) { SYS_CONSOLE_PRINT("OpenSpi failed\r\n"); goto restore; }
    rcp_set_async_timeout_ms(300);
    okX = mcp3204_read(h, &wid, 1, &x);   /* ch1 = X */
    okY = mcp3204_read(h, &wid, 0, &y);   /* ch0 = Y */
    if (okX && okY) {
        SYS_CONSOLE_PRINT("  channel 1 (X): raw=%4u (0x%03X)\r\n", x, x);
        SYS_CONSOLE_PRINT("  channel 0 (Y): raw=%4u (0x%03X)\r\n", y, y);
        SYS_CONSOLE_PRINT("  -> MCP3204 Thumbstick responding (no ID reg; ~2048/axis at rest is the fingerprint).\r\n");
    } else SYS_CONSOLE_PRINT("  read failed (X=%s Y=%s). Thumbstick in slot 4 / SPI routed?\r\n", okX ? "ok" : "FAIL", okY ? "ok" : "FAIL");
    rcp_set_async_timeout_ms(150);
    spi_close(h);
restore:
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

/* --- thumbmon [secs] : continuous thumbstick monitor (thumbmon.c) ---------- */
static volatile int s_tm_done, s_tm_ok; static volatile uint16_t s_tm_x, s_tm_y;
static void on_tm(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{
    (void)ctx; s_tm_done = 1; s_tm_ok = 0;
    if (rc == RT_OK && rx) {
        uint8_t r0[3] = {0}, r1[3] = {0}; uint16_t l0 = 3, l1 = 3;
        if (rcp_dec_spi2(rx, rxLen, r0, &l0, r1, &l1) && l0 >= 3 && l1 >= 3) {
            s_tm_y = (uint16_t)(((r0[1] << 8) | r0[2]) & ADC_MAX);
            s_tm_x = (uint16_t)(((r1[1] << 8) | r1[2]) & ADC_MAX);
            s_tm_ok = 1;
        }
    }
}
static void cmd_thumbmon(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    uint16_t h; uint32_t wid = 0u, secs = 20u, start, endt, lastP = 0u;
    SYS_CONSOLE_HANDLE con = SYS_CONSOLE_HandleGet(SYS_CONSOLE_INDEX_0);
    int aborted = 0;
    (void)pCmdIO;
    if (argc >= 2) secs = (uint32_t)strtoul(argv[1], NULL, 10);
    if (secs < 1u) secs = 1u; if (secs > 600u) secs = 600u;

    if (!sel_first_ep()) { SYS_CONSOLE_PRINT("[thumbmon] no endpoint - run 'discovery' first\r\n"); return; }
    rcp_set_timeout_ms(800); rcp_set_retries(2);
    if (!spi_open(SPI_MISO, SPI_SCK, SPI_CS, SPI_MOSI, 1u, 1923000u, &h)) { SYS_CONSOLE_PRINT("OpenSpi failed\r\n"); goto restore; }
    rcp_set_async_timeout_ms(120);
    SYS_CONSOLE_PRINT("Thumbstick monitor for %u s (move the stick; 'q' to stop)...\r\n", (unsigned)secs);
    start = plat_now_ms(); endt = start + secs * 1000u;
    while (!aborted && (int32_t)(plat_now_ms() - endt) < 0) {
        uint8_t pp[64], c0[3] = { 0x06, 0x00, 0xFF }, c1[3] = { 0x06, 0x40, 0xFF }; uint16_t n;
        s_tm_done = 0;
        n = rcp_enc_spi2(pp, sizeof(pp), h, wid++, c0, 3, c1, 3, 3, 3);
        if (n && rcp_async_request(0x1509u, pp, n, on_tm, NULL) == RT_OK)
            while (!s_tm_done) { rcp_async_poll(); plat_sleep_ms(2); if (chk_abort(con)) { aborted = 1; break; } }
        { uint32_t now = plat_now_ms();
          if (now - lastP >= 150u) { SYS_CONSOLE_PRINT("\r  X=%4u Y=%4u %s   ", s_tm_x, s_tm_y, s_tm_ok ? "  " : ".."); lastP = now; } }
        if (chk_abort(con)) aborted = 1;
        plat_sleep_ms(30);
    }
    SYS_CONSOLE_PRINT("\r\nDone.\r\n");
    rcp_set_async_timeout_ms(150);
    spi_close(h);
restore:
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

/* --- adc [channel 0|1] [vref 0|1] (mirrors adc.c) -------------------------- */
static void cmd_adc(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    OpenAdcVar_t ov; OpenAdcReply_t orep; CloseAdcVar_t cv; ReadAdcVar_t rv; ReadAdcReply_t rr;
    uint8_t channel = 0u, vref = 0u; uint32_t mvFull;
    (void)pCmdIO;
    if (argc >= 2) channel = (uint8_t)strtoul(argv[1], NULL, 10);
    if (argc >= 3) vref    = (uint8_t)strtoul(argv[2], NULL, 10);

    if (!sel_first_ep()) { SYS_CONSOLE_PRINT("[adc] no endpoint - run 'discovery' first\r\n"); return; }
    rcp_set_timeout_ms(800); rcp_set_retries(2);
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep)); ov.PinId = 0;
    if (rcp_open_adc(&ov, &orep) != RT_OK) { SYS_CONSOLE_PRINT("OpenAdc failed (ADC not configured?)\r\n"); goto restore; }
    mvFull = (vref == 1u) ? 1100u : 3300u;
    memset(&rv, 0, sizeof(rv)); memset(&rr, 0, sizeof(rr));
    rv.HandleAdc = orep.HandleAdc; rv.ChannelSelect = channel; rv.VoltageReference = vref;
    if (rcp_read_adc(&rv, &rr) == RT_OK) {
        if (channel == 1u) SYS_CONSOLE_PRINT("  raw=%4u  (internal temperature sensor)\r\n", rr.ReadData);
        else SYS_CONSOLE_PRINT("  raw=%4u = %u mV  (ref %s)\r\n", rr.ReadData,
                 (unsigned)((uint32_t)rr.ReadData * mvFull / 4095u), vref == 1u ? "1V1" : "3V3");
    } else SYS_CONSOLE_PRINT("ReadAdc failed\r\n");
    memset(&cv, 0, sizeof(cv)); cv.HandleAdc = orep.HandleAdc; rcp_close_adc(&cv);
restore:
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

/* --- pwm <pin> [freqHz] [dutyPct] [holdSecs] (mirrors pwm.c) --------------- */
static void cmd_pwm(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    ReleaseDigitalPinsVar_t rel; OpenPwmVar_t ov; OpenPwmReply_t orep; ClosePwmVar_t cv;
    uint8_t pin = 6u; uint32_t freq = 1000u, duty = 50u, periodNs, dutyQ31; int hold = -1;
    (void)pCmdIO;
    if (argc >= 2) pin  = (uint8_t)strtoul(argv[1], NULL, 10);
    if (argc >= 3) freq = (uint32_t)strtoul(argv[2], NULL, 10);
    if (argc >= 4) duty = (uint32_t)strtoul(argv[3], NULL, 10);
    if (argc >= 5) hold = (int)strtoul(argv[4], NULL, 10);
    if (freq < 1u) freq = 1000u; if (duty > 100u) duty = 100u;
    periodNs = (uint32_t)(1000000000ULL / freq);
    dutyQ31 = (uint32_t)(((uint64_t)duty * 2147483648ULL) / 100ULL);

    if (!sel_first_ep()) { SYS_CONSOLE_PRINT("[pwm] no endpoint - run 'discovery' first\r\n"); return; }
    rcp_set_timeout_ms(800); rcp_set_retries(2);
    memset(&rel, 0, sizeof(rel)); rel.PinIdList[0] = pin; rel.PinIdListLength = 1;
    rcp_release_digital_pins(&rel);
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinId = pin; ov.IntervalTime = periodNs; ov.DutyCycle = dutyQ31;
    if (rcp_open_pwm(&ov, &orep) != RT_OK) { SYS_CONSOLE_PRINT("OpenPwm failed (PWM not configured on this pin/build?)\r\n"); goto restore; }
    SYS_CONSOLE_PRINT("PWM open: PA%02u period=%u ns (%u Hz) duty=%u%%\r\n",
                      (unsigned)pin, (unsigned)periodNs, (unsigned)freq, (unsigned)duty);
    if (hold >= 0) {
        SYS_CONSOLE_PRINT("Holding %d s, then stopping...\r\n", hold);
        plat_sleep_ms((uint32_t)hold * 1000u);
        memset(&cv, 0, sizeof(cv)); cv.HandlePwm = orep.HandlePwm; rcp_close_pwm(&cv);
        SYS_CONSOLE_PRINT("PWM stopped.\r\n");
    } else {
        SYS_CONSOLE_PRINT("Signal left running. Re-run with duty 0 or a hold time to stop.\r\n");
    }
restore:
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

static const SYS_CMD_DESCRIPTOR spi_cmd_tbl[] = {
    {"spi",      (SYS_CMD_FNC) cmd_spi,      ": SPI full-duplex transfer (spi <txhex> [mode] [speedHz])"},
    {"spiid",    (SYS_CMD_FNC) cmd_spiid,    ": identify the Thumbstick MCP3204 over SPI"},
    {"thumbmon", (SYS_CMD_FNC) cmd_thumbmon, ": monitor the thumbstick axes (thumbmon [secs])"},
    {"adc",      (SYS_CMD_FNC) cmd_adc,      ": read the on-chip ADC (adc [channel 0|1] [vref 0|1])"},
    {"pwm",      (SYS_CMD_FNC) cmd_pwm,      ": drive a PWM output (pwm <pin> [freqHz] [dutyPct] [holdSecs])"},
};

void SPI_CLI_Init(void)
{
    SYS_CMD_ADDGRP(spi_cmd_tbl, sizeof(spi_cmd_tbl) / sizeof(*spi_cmd_tbl), "spi", ": SPI/ADC/PWM demos");
}
