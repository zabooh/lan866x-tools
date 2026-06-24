/*
 * clickdemo_cli.c - MikroE Click demo on the bridge CLI (port of clickdemo.c).
 *
 * Drives two 10x10 RGB Click displays on a LAN866x lighting endpoint from two
 * sensors, exactly like the host tool:
 *   - left  display <- Thumbstick Click (SPI, MCP3204)  -> orange spot
 *   - right display <- Proximity 3 Click (I2C, VCNL4200) -> blue distance bar
 * The displays are rendered by the endpoint from an RTP/RFC4175 stream on UDP
 * 5001, so we build a 20x10 RGB frame and send it each tick; sensors are read
 * asynchronously over RCP (compound SPI 0x1509, WriteAndReadI2C 0x1208).
 *
 * Firmware adaptations vs the host tool:
 *   - BOUNDED run (seconds arg) instead of run-until-Ctrl-C, so it never freezes
 *     the bridge superloop. plat_sleep_ms() pumps the TCP/IP stack + console
 *     during the frame cadence, so bridging keeps running throughout.
 *   - no CSV event log (no filesystem); SYS_TIME instead of Win32 timing;
 *     static frame/packet buffers (kept off the small CLI stack).
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "definitions.h"
#include "config/default/system/console/sys_console.h"
#include "rcp.h"
#include "plat.h"
#include "lan866x_cli.h"

#define X_RES   20
#define Y_RES   10
#define RTP_PORT 5001
#define RTP_TYPE 96
#define ADC_MAX  0x0FFF

#define SPI_MISO 12u
#define SPI_SCK  13u
#define SPI_CS   14u
#define SPI_MOSI 15u
#define I2C_SDA   8u
#define I2C_SCL   9u

#define VCNL4200_ADDR     0x51u
#define VCNL4200_ID_VALUE 0x1058u
#define VCNL4200_PS_CONF1 0x03u
#define VCNL4200_PS_CONF3 0x04u
#define VCNL4200_ALS_CONF 0x00u
#define VCNL4200_PS_DATA  0x08u
#define VCNL4200_ID_REG   0x0Eu

static uint8_t     s_fb[Y_RES][X_RES][3];
static uint8_t     s_pkt[64 + Y_RES * 6 + Y_RES * X_RES * 3];
static plat_udp_t *s_rtp = NULL;
static uint8_t     s_ip[4];
static uint32_t    s_seq = 0u, s_ssrc = 0u;

static uint16_t s_spi = UINT16_MAX; static uint32_t s_spiWid = 0u;
static uint16_t s_i2c = UINT16_MAX; static uint32_t s_i2cWid = 0u;
static int      s_proxInit = 0;

static volatile uint16_t s_tx = ADC_MAX / 2, s_ty = ADC_MAX / 2, s_prox = 0u;
static volatile int s_thumbPending = 0, s_proxPending = 0;
static volatile int s_thumbOk = 0, s_proxOk = 0;

static uint32_t clk_us10(void)   /* RTP timestamp tick: 1/10 microsecond units */
{
    uint32_t f = SYS_TIME_FrequencyGet();
    if (f == 0u) return 0u;
    return (uint32_t)(((SYS_TIME_Counter64Get() * 1000000ULL) / (uint64_t)f) / 10ULL);
}

static void rtp_rx(plat_udp_t *s, const uint8_t ip[4], uint16_t port,
                   const uint8_t *buf, uint16_t len, void *tag)
{ (void)s; (void)ip; (void)port; (void)buf; (void)len; (void)tag; }

/* RFC4175: send columns [x0 .. x0+w) of the frame in one RTP packet. */
static void rtp_send_region(int x0, int w)
{
    int n = 0, x, y;
    uint32_t ts = clk_us10();

    s_pkt[n++] = 0x80;
    s_pkt[n++] = (uint8_t)(0x80u | (RTP_TYPE & 0x7Fu));
    s_pkt[n++] = (uint8_t)(s_seq >> 8);  s_pkt[n++] = (uint8_t)s_seq;
    s_pkt[n++] = (uint8_t)(ts >> 24); s_pkt[n++] = (uint8_t)(ts >> 16);
    s_pkt[n++] = (uint8_t)(ts >> 8);  s_pkt[n++] = (uint8_t)ts;
    s_pkt[n++] = (uint8_t)(s_ssrc >> 24); s_pkt[n++] = (uint8_t)(s_ssrc >> 16);
    s_pkt[n++] = (uint8_t)(s_ssrc >> 8);  s_pkt[n++] = (uint8_t)s_ssrc;
    s_pkt[n++] = (uint8_t)(s_seq >> 24); s_pkt[n++] = (uint8_t)(s_seq >> 16);

    for (y = 0; y < Y_RES; ++y) {
        int cont = (y != (Y_RES - 1));
        uint16_t len = (uint16_t)(w * 3);
        s_pkt[n++] = (uint8_t)(len >> 8);  s_pkt[n++] = (uint8_t)len;
        s_pkt[n++] = (uint8_t)(y >> 8);    s_pkt[n++] = (uint8_t)y;
        s_pkt[n++] = (uint8_t)((cont ? 0x80 : 0x00) | ((x0 >> 8) & 0x7F));
        s_pkt[n++] = (uint8_t)x0;
    }
    for (y = 0; y < Y_RES; ++y)
        for (x = x0; x < x0 + w; ++x) {
            s_pkt[n++] = s_fb[y][x][0]; s_pkt[n++] = s_fb[y][x][1]; s_pkt[n++] = s_fb[y][x][2];
        }

    plat_udp_send(s_rtp, s_ip, RTP_PORT, s_pkt, (uint16_t)n);
    s_seq++;
}
static void rtp_send(void) { rtp_send_region(0, X_RES); }

/* --- Thumbstick (MCP3204 over SPI) --- */
static int spi_setup(void)
{
    ReleaseDigitalPinsVar_t rel; OpenSpiVar_t ov; OpenSpiReply_t orep;
    uint16_t p = 0;
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    rel.PinIdList[p++] = SPI_MISO; rel.PinIdList[p++] = SPI_SCK;
    rel.PinIdList[p++] = SPI_CS;   rel.PinIdList[p++] = SPI_MOSI;
    rel.PinIdListLength = p; rcp_release_digital_pins(&rel);
    plat_sleep_ms(25);
    ov.PinIdMiso = SPI_MISO; ov.PinIdSck = SPI_SCK; ov.PinIdCs = SPI_CS;
    ov.PinIdMosi = SPI_MOSI; ov.Mode = 1; ov.ClockSpeed = 1923000u;
    s_spiWid = 0u;
    if (rcp_open_spi(&ov, &orep) != RT_OK) { s_spi = UINT16_MAX; return 0; }
    s_spi = orep.HandleSpi;
    return 1;
}

/* --- Proximity 3 (VCNL4200 over I2C) --- */
static int i2c_setup(void)
{
    ReleaseDigitalPinsVar_t rel; OpenI2CVar_t ov; OpenI2CReply_t orep;
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    rel.PinIdList[0] = I2C_SDA; rel.PinIdList[1] = I2C_SCL; rel.PinIdListLength = 2;
    rcp_release_digital_pins(&rel);
    plat_sleep_ms(25);
    ov.PinIdSda = I2C_SDA; ov.PinIdScl = I2C_SCL; ov.ClockSpeed = 1;
    s_i2cWid = 0u; s_proxInit = 0;
    if (rcp_open_i2c(&ov, &orep) != RT_OK) { s_i2c = UINT16_MAX; return 0; }
    s_i2c = orep.HandleI2C;
    return 1;
}

static int i2c_write(const uint8_t *tx, uint16_t n)
{
    WriteI2CVar_t w; memset(&w, 0, sizeof(w));
    w.HandleI2C = s_i2c; w.DeviceAddress = VCNL4200_ADDR; w.WriteId = s_i2cWid++;
    w.WriteDataLength = n; memcpy(w.WriteData, tx, n);
    return rcp_write_i2c(&w) == RT_OK;
}

static int i2c_read_reg(uint8_t reg, uint16_t *val)
{
    WriteAndReadI2CVar_t rq; ReadI2CReply_t rp;
    memset(&rq, 0, sizeof(rq)); memset(&rp, 0, sizeof(rp));
    rq.HandleI2C = s_i2c; rq.DeviceAddress = VCNL4200_ADDR; rq.ReadDataLength = 2;
    rq.WriteId = s_i2cWid++; rq.WriteDataLength = 1; rq.WriteData[0] = reg;
    if (rcp_write_and_read_i2c(&rq, &rp) != RT_OK || rp.ReadDataLength < 2) return 0;
    *val = (uint16_t)(rp.ReadData[0] | (rp.ReadData[1] << 8));
    return 1;
}

static int vcnl4200_init(void)
{
    uint16_t id = 0; uint8_t b[3];
    if (!i2c_read_reg(VCNL4200_ID_REG, &id) || id != VCNL4200_ID_VALUE) return 0;
    b[0] = VCNL4200_PS_CONF1; b[1] = 0x02; b[2] = 0x08;
    plat_sleep_ms(25); if (!i2c_write(b, 3)) return 0;
    b[0] = VCNL4200_PS_CONF3; b[1] = 0x01; b[2] = 0x07;
    plat_sleep_ms(25); if (!i2c_write(b, 3)) return 0;
    b[0] = VCNL4200_ALS_CONF; b[1] = 0x00; b[2] = 0x00;
    plat_sleep_ms(25); if (!i2c_write(b, 3)) return 0;
    return 1;
}

/* --- render --- */
static void thumb_spot(uint16_t xr, uint16_t yr, int bright)
{
    int x, y;
    double cx = (double)(ADC_MAX - xr) * 9.0 / (double)ADC_MAX;
    double cy = (double)(ADC_MAX - yr) * 9.0 / (double)ADC_MAX;
    const double r2 = 6.25;
    for (y = 0; y < Y_RES; ++y)
        for (x = 0; x < 10; ++x) {
            double dx = (double)x - cx, dy = (double)y - cy;
            double in = 1.0 - (dx * dx + dy * dy) / r2;
            if (in < 0.0) in = 0.0;
            s_fb[y][x][0] = (uint8_t)((double)bright * in);
            s_fb[y][x][1] = (uint8_t)((double)bright * in * 0.55);
            s_fb[y][x][2] = 0;
        }
}

static void prox_bar(uint16_t raw, int full, int blue)
{
    static int row = Y_RES - 1;
    const int lo = 2;
    int x, y;
    if (full <= lo) full = lo + 1;
    if (raw != 0u) {
        int v = (int)raw;
        if (v < lo) v = lo; else if (v > full) v = full;
        row = (Y_RES - 1) * (full - v) / (full - lo);
    }
    for (y = 0; y < Y_RES; ++y)
        for (x = 10; x < X_RES; ++x) {
            s_fb[y][x][0] = 0; s_fb[y][x][1] = 0;
            s_fb[y][x][2] = (y == row) ? (uint8_t)blue : 0u;
        }
}

/* --- async sensor reads --- */
static void on_thumb(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{
    (void)ctx;
    if (rc == RT_OK && rx) {
        uint8_t r0[3] = {0}, r1[3] = {0}; uint16_t l0 = 3, l1 = 3;
        if (rcp_dec_spi2(rx, rxLen, r0, &l0, r1, &l1) && l0 >= 3 && l1 >= 3) {
            s_ty = (uint16_t)(((r0[1] << 8) | r0[2]) & 0x0FFF);
            s_tx = (uint16_t)(((r1[1] << 8) | r1[2]) & 0x0FFF);
            s_thumbOk = 1;
        } else s_thumbOk = 0;
    } else s_thumbOk = 0;
    s_thumbPending = 0;
}
static void on_prox(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{
    (void)ctx;
    if (rc == RT_OK && rx) {
        uint8_t d[8] = {0}; uint16_t l = sizeof(d);
        if (rcp_dec_i2c_read(rx, rxLen, d, &l) && l >= 2) {
            s_prox = (uint16_t)(d[0] | (d[1] << 8));
            s_proxOk = 1;
        } else s_proxOk = 0;
    } else s_proxOk = 0;
    s_proxPending = 0;
}

static int fire_thumb(void)
{
    uint8_t params[64], c0[3] = { 0x06, 0x00, 0xFF }, c1[3] = { 0x06, 0x40, 0xFF };
    uint16_t pl;
    if (s_spi == UINT16_MAX || s_thumbPending) return 0;
    pl = rcp_enc_spi2(params, sizeof(params), s_spi, s_spiWid++, c0, 3, c1, 3, 3, 3);
    s_thumbPending = 1;
    if (pl && rcp_async_request(0x1509u, params, pl, on_thumb, NULL) == RT_OK) return 1;
    s_thumbPending = 0; return 0;
}
static int fire_prox(void)
{
    uint8_t params[64], reg = VCNL4200_PS_DATA;
    uint16_t pl;
    if (!s_proxInit || s_proxPending) return 0;
    pl = rcp_enc_i2c_read(params, sizeof(params), s_i2c, VCNL4200_ADDR, s_i2cWid++, &reg, 1, 2);
    s_proxPending = 1;
    if (pl && rcp_async_request(0x1208u, params, pl, on_prox, NULL) == RT_OK) return 1;
    s_proxPending = 0; return 0;
}

void clickdemo_run(uint32_t seconds, int fps, int bright, int proxMax, int barBlue)
{
    rcp_endpoint_t eps[RCP_MAX_ENDPOINTS];
    uint16_t rtpPort = 0u;
    uint32_t start, lastPrint = 0u, tick = 0u, step;
    int i;

    if (fps < 1) fps = 1; if (fps > 100) fps = 100;
    if (bright < 1) bright = 1; if (bright > 255) bright = 255;
    if (proxMax < 1) proxMax = 1;
    if (barBlue < 0) barBlue = 0; if (barBlue > 255) barBlue = 255;
    step = (uint32_t)(1000 / fps);

    if (rcp_get_endpoints(eps, RCP_MAX_ENDPOINTS) == 0u || !rcp_select_endpoint(0u)) {
        SYS_CONSOLE_PRINT("[clickdemo] no endpoint - run 'discovery' first\r\n");
        return;
    }
    memcpy(s_ip, eps[0].ip, 4);

    s_rtp = plat_udp_open(&rtpPort, rtp_rx, NULL);
    if (!s_rtp) { SYS_CONSOLE_PRINT("[clickdemo] RTP socket failed\r\n"); return; }
    s_ssrc = plat_now_ms();
    s_spi = UINT16_MAX; s_i2c = UINT16_MAX; s_proxInit = 0;
    s_tx = ADC_MAX / 2; s_ty = ADC_MAX / 2; s_prox = 0;

    if (seconds == 0u) {
        SYS_CONSOLE_PRINT("[clickdemo] target %u.%u.%u.%u, RTP :%d, %d fps, runs until Ctrl-C or 'q'\r\n",
                          s_ip[0], s_ip[1], s_ip[2], s_ip[3], RTP_PORT, fps);
    } else {
        SYS_CONSOLE_PRINT("[clickdemo] target %u.%u.%u.%u, RTP :%d, %d fps, %u s (Ctrl-C or 'q' to stop)\r\n",
                          s_ip[0], s_ip[1], s_ip[2], s_ip[3], RTP_PORT, fps, (unsigned)seconds);
    }

    /* blocking one-time peripheral setup (generous timeout/retries) */
    rcp_set_timeout_ms(600); rcp_set_retries(2);
    for (i = 0; i < 5 && s_spi == UINT16_MAX; ++i) { spi_setup(); if (s_spi == UINT16_MAX) plat_sleep_ms(50); }
    plat_sleep_ms(40);
    for (i = 0; i < 5 && !s_proxInit; ++i) {
        if (s_i2c == UINT16_MAX) i2c_setup();
        if (s_i2c != UINT16_MAX) s_proxInit = vcnl4200_init();
        if (!s_proxInit) plat_sleep_ms(50);
    }
    SYS_CONSOLE_PRINT("[clickdemo] setup: thumbstick(SPI)=%s  proximity(I2C)=%s\r\n",
                      s_spi != UINT16_MAX ? "OK" : "FAILED", s_proxInit ? "OK" : "FAILED");

    rcp_set_async_timeout_ms(70);
    start = plat_now_ms();
    {
    SYS_CONSOLE_HANDLE con = SYS_CONSOLE_HandleGet(SYS_CONSOLE_INDEX_0);
    int aborted = 0;
    /* seconds == 0 -> run until the user aborts (Ctrl-C / 'q'); else time-bounded. */
    while (!aborted && (seconds == 0u || (plat_now_ms() - start) < seconds * 1000u)) {
        /* Abort on Ctrl-C (0x03) or 'q' typed in the terminal. SYS_CMD_Tasks is
         * not running while we block here, so the bytes sit in the console RX
         * ring (filled by SYS_CONSOLE_Tasks, pumped from plat_sleep_ms); read
         * them directly. */
        { char ch; while (SYS_CONSOLE_Read(con, &ch, 1) > 0) {
              if (ch == 0x03 || ch == 'q' || ch == 'Q') aborted = 1; } }

        thumb_spot(s_tx, s_ty, bright);
        prox_bar(s_prox, proxMax, barBlue);
        rtp_send();

        if (tick & 1u) { if (!fire_prox())  fire_thumb(); }
        else           { if (!fire_thumb()) fire_prox();  }
        tick++;

        rcp_async_poll();

        {
            uint32_t now = plat_now_ms();
            if (now - lastPrint >= 200u) {
                SYS_CONSOLE_PRINT("\r  Thumb x=%4u y=%4u %s | Prox raw=%5u %s  ",
                                  s_tx, s_ty, s_thumbOk ? "  " : "..", s_prox, s_proxOk ? "  " : "..");
                lastPrint = now;
            }
        }
        plat_sleep_ms(step);
    }
    if (aborted) SYS_CONSOLE_PRINT("\r\n[clickdemo] aborted by user\r\n");
    }

    /* clear both displays, release peripherals */
    SYS_CONSOLE_PRINT("\r\n[clickdemo] stopping - clearing displays\r\n");
    memset(s_fb, 0, sizeof(s_fb));
    rtp_send(); plat_sleep_ms(30); rtp_send();
    if (s_spi != UINT16_MAX) { CloseSpiVar_t c; c.HandleSpi = s_spi; rcp_close_spi(&c); }
    if (s_i2c != UINT16_MAX) { CloseI2CVar_t c; c.HandleI2C = s_i2c; rcp_close_i2c(&c); }
    plat_udp_close(s_rtp); s_rtp = NULL;
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}
