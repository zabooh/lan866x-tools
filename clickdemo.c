/*
 * clickdemo.c  -  Interactive MikroE Click demo for a LAN866x lighting endpoint.
 *                 Pure C, Windows.
 *
 * Two 10x10 RGB Click displays (WS2812) are driven from two sensors:
 *   - RGB Click in slot 1 (left half)  <- Thumbstick Click  (SPI, slot 4)
 *   - RGB Click in slot 2 (right half) <- Proximity 3 Click (I2C, slot 3)
 *
 * Sensors are read over the RCP service (OpenSpi/WriteAndReadSpi for the
 * MCP3204 in the Thumbstick; OpenI2C/WriteI2C/WriteAndReadI2C for the VCNL4200
 * in the Proximity 3). The displays are not directly addressable - the lighting
 * firmware renders them from an RTP/RFC4175 video stream on UDP port 5001, so we
 * build a 20x10 RGB frame (left 10 columns = display 1, right 10 = display 2)
 * and send it each loop. This mirrors the official "gameloop-example".
 *
 *   lan866x-clickdemo --ip 192.168.0.54
 *
 * Pin map (matches the demo config / board DIP defaults):
 *   Thumbstick SPI : MISO=PA12 SCK=PA13 CS=PA14 MOSI=PA15  (mode 1, 1.923 MHz)
 *   Proximity  I2C : SDA=PA08 SCL=PA09  (400 kHz), VCNL4200 @ 0x51
 *
 * Ctrl-C to stop (clears both displays first).
 */

#include <winsock2.h>     /* before any windows.h pulled in by tool_common.h */
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

/* --- frame / RTP -------------------------------------------------------- */
#define X_RES       20
#define Y_RES       10
#define RTP_PORT    5001
#define RTP_TYPE    96            /* RFC4175 dynamic payload type (default 96) */
#define ADC_MAX     0x0FFF        /* MCP3204 is 12-bit */

/* --- pins (PA index == pin id) ------------------------------------------ */
#define SPI_MISO 12u
#define SPI_SCK  13u
#define SPI_CS   14u
#define SPI_MOSI 15u
#define I2C_SDA   8u
#define I2C_SCL   9u

#define VCNL4200_ADDR        0x51u
#define VCNL4200_ID_VALUE    0x1058u
#define VCNL4200_PS_CONF1    0x03u
#define VCNL4200_PS_CONF3    0x04u
#define VCNL4200_ALS_CONF    0x00u
#define VCNL4200_PS_DATA     0x08u
#define VCNL4200_ID_REG      0x0Eu

static uint8_t  s_fb[Y_RES][X_RES][3];
static SOCKET   s_rtp = INVALID_SOCKET;
static uint8_t  s_ip[4];
static uint32_t s_seq = 0, s_ssrc = 0;
static volatile int s_run = 1;

static void on_sigint(int sig) { (void)sig; s_run = 0; }

static uint32_t now_us10(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint32_t)(((c.QuadPart * 1000000ULL) / (uint64_t)f.QuadPart) / 10ULL);
}

/* --- RTP/RFC4175: send the whole 20x10 frame in a single packet --------- */
static void rtp_send(void)
{
    uint8_t pkt[64 + Y_RES * 6 + Y_RES * X_RES * 3];
    int n = 0, x, y;
    uint32_t ts = now_us10();
    struct sockaddr_in dst;

    /* RTP header (12B). Single packet => marker=1. */
    pkt[n++] = 0x80;                                  /* V=2, no pad/ext/cc   */
    pkt[n++] = (uint8_t)(0x80u | (RTP_TYPE & 0x7Fu)); /* marker + payload type*/
    pkt[n++] = (uint8_t)(s_seq >> 8);  pkt[n++] = (uint8_t)s_seq;
    pkt[n++] = (uint8_t)(ts >> 24); pkt[n++] = (uint8_t)(ts >> 16);
    pkt[n++] = (uint8_t)(ts >> 8);  pkt[n++] = (uint8_t)ts;
    pkt[n++] = (uint8_t)(s_ssrc >> 24); pkt[n++] = (uint8_t)(s_ssrc >> 16);
    pkt[n++] = (uint8_t)(s_ssrc >> 8);  pkt[n++] = (uint8_t)s_ssrc;
    /* RFC4175 extended sequence number (high 16 bits of the 32-bit seq) */
    pkt[n++] = (uint8_t)(s_seq >> 24); pkt[n++] = (uint8_t)(s_seq >> 16);

    /* one RFC4175 line header per scan line (length, lineNr, offset+cont) */
    for (y = 0; y < Y_RES; ++y) {
        int cont = (y != (Y_RES - 1));
        uint16_t len = (uint16_t)(X_RES * 3);
        pkt[n++] = (uint8_t)(len >> 8);  pkt[n++] = (uint8_t)len;
        pkt[n++] = (uint8_t)(y >> 8);    pkt[n++] = (uint8_t)y;
        pkt[n++] = (uint8_t)(cont ? 0x80 : 0x00); pkt[n++] = 0x00; /* offset 0 */
    }
    /* pixel data, line after line */
    for (y = 0; y < Y_RES; ++y)
        for (x = 0; x < X_RES; ++x) {
            pkt[n++] = s_fb[y][x][0];
            pkt[n++] = s_fb[y][x][1];
            pkt[n++] = s_fb[y][x][2];
        }

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(RTP_PORT);
    dst.sin_addr.s_addr = (uint32_t)s_ip[0] | ((uint32_t)s_ip[1] << 8) |
                          ((uint32_t)s_ip[2] << 16) | ((uint32_t)s_ip[3] << 24);
    sendto(s_rtp, (const char *)pkt, n, 0, (struct sockaddr *)&dst, sizeof(dst));
    s_seq++;
}

/* fill one display half [x0 .. x0+9] with a solid RGB colour */
static void fill_half(int x0, uint8_t r, uint8_t g, uint8_t b)
{
    int x, y;
    for (y = 0; y < Y_RES; ++y)
        for (x = x0; x < x0 + 10 && x < X_RES; ++x) {
            s_fb[y][x][0] = r; s_fb[y][x][1] = g; s_fb[y][x][2] = b;
        }
}

/* --- Thumbstick (MCP3204 over SPI) -------------------------------------- */
static uint16_t s_spi = UINT16_MAX;
static uint32_t s_spiWid = 0;

static int spi_setup(void)
{
    ReleaseDigitalPinsVar_t rel; OpenSpiVar_t ov; OpenSpiReply_t orep;
    uint16_t p = 0;
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    rel.PinIdList[p++] = SPI_MISO; rel.PinIdList[p++] = SPI_SCK;
    rel.PinIdList[p++] = SPI_CS;   rel.PinIdList[p++] = SPI_MOSI;
    rel.PinIdListLength = p; rcp_release_digital_pins(&rel);
    ov.PinIdMiso = SPI_MISO; ov.PinIdSck = SPI_SCK; ov.PinIdCs = SPI_CS;
    ov.PinIdMosi = SPI_MOSI; ov.Mode = 1; ov.ClockSpeed = 1923000u;
    s_spiWid = 0;
    if (rcp_open_spi(&ov, &orep) != RT_OK) { s_spi = UINT16_MAX; return 0; }
    s_spi = orep.HandleSpi;
    return 1;
}

static int spi_read_channel(uint8_t ch, uint16_t *val)
{
    WriteAndReadSpiVar_t rq; WriteAndReadSpiReply_t rp;
    memset(&rq, 0, sizeof(rq)); memset(&rp, 0, sizeof(rp));
    rq.HandleSpi = s_spi; rq.ReadDataLength = 3; rq.WriteId = s_spiWid++;
    rq.WriteDataLength = 3;
    rq.WriteData[0] = 0x06; rq.WriteData[1] = (uint8_t)(ch << 6); rq.WriteData[2] = 0xFF;
    if (rcp_write_and_read_spi(&rq, &rp) != RT_OK || rp.ReadDataLength < 3) return 0;
    *val = (uint16_t)(((rp.ReadData[1] << 8) | rp.ReadData[2]) & 0x0FFF);
    return 1;
}

/* y = channel 0, x = channel 1 (as in the official demo) */
static int thumbstick_read(uint16_t *x, uint16_t *y)
{
    if (s_spi == UINT16_MAX && !spi_setup()) return 0;
    if (!spi_read_channel(0, y)) { s_spi = UINT16_MAX; return 0; }
    if (!spi_read_channel(1, x)) { s_spi = UINT16_MAX; return 0; }
    return 1;
}

/* --- Proximity 3 (VCNL4200 over I2C) ------------------------------------ */
static uint16_t s_i2c = UINT16_MAX;
static uint32_t s_i2cWid = 0;
static int      s_proxInit = 0;

static int i2c_setup(void)
{
    ReleaseDigitalPinsVar_t rel; OpenI2CVar_t ov; OpenI2CReply_t orep;
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    rel.PinIdList[0] = I2C_SDA; rel.PinIdList[1] = I2C_SCL; rel.PinIdListLength = 2;
    rcp_release_digital_pins(&rel);
    ov.PinIdSda = I2C_SDA; ov.PinIdScl = I2C_SCL; ov.ClockSpeed = 1; /* 400 kHz */
    s_i2cWid = 0; s_proxInit = 0;
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
    *val = (uint16_t)(rp.ReadData[0] | (rp.ReadData[1] << 8)); /* VCNL4200 = LSB first */
    return 1;
}

static int vcnl4200_init(void)
{
    uint16_t id = 0;
    uint8_t b[3];
    if (!i2c_read_reg(VCNL4200_ID_REG, &id) || id != VCNL4200_ID_VALUE) return 0;
    /* PS_CONF1/2: IT 9T | duty 1/320 ; AF enable */
    b[0] = VCNL4200_PS_CONF1; b[1] = 0x0A | 0x40; b[2] = 0x08;
    if (!i2c_write(b, 3)) return 0;
    /* PS_CONF3/MS: sunlight cancel enable ; LED 50 mA */
    b[0] = VCNL4200_PS_CONF3; b[1] = 0x01; b[2] = 0x00;
    if (!i2c_write(b, 3)) return 0;
    /* ALS_CONF: enabled */
    b[0] = VCNL4200_ALS_CONF; b[1] = 0x00; b[2] = 0x00;
    if (!i2c_write(b, 3)) return 0;
    return 1;
}

static int proximity_read(uint16_t *prox)
{
    if (s_i2c == UINT16_MAX && !i2c_setup()) return 0;
    if (!s_proxInit) { s_proxInit = vcnl4200_init(); if (!s_proxInit) { s_i2c = UINT16_MAX; return 0; } }
    if (!i2c_read_reg(VCNL4200_PS_DATA, prox)) { s_i2c = UINT16_MAX; return 0; }
    return 1;
}

/* ------------------------------------------------------------------------ */
static uint8_t scale(uint32_t v, uint32_t vmax, uint32_t out)
{
    if (v > vmax) v = vmax;
    return (uint8_t)(vmax ? (v * out / vmax) : 0u);
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, fps = 30, sel, bright = 128, proxDiv = 16;
    rcp_endpoint_t eps[RCP_MAX_ENDPOINTS];
    WSADATA wsa;
    uint16_t tx = ADC_MAX / 2, ty = ADC_MAX / 2, prox = 0;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-clickdemo - drive two RGB Click displays from Thumbstick + Proximity\n\n"
                   "  lan866x-clickdemo [--ip <addr>|--ep <i>] [--fps N] [--bright 0..255] [--prox-div N]\n\n"
                   "  Left display  (slot 1) follows the Thumbstick (slot 4, SPI).\n"
                   "  Right display (slot 2) follows the Proximity 3 (slot 3, I2C).\n"
                   "  --prox-div: larger = less sensitive proximity (default 16).\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")       && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")       && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fps")      && i+1<argc) fps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bright")   && i+1<argc) bright = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--prox-div") && i+1<argc) proxDiv = atoi(argv[++i]);
    }
    if (fps < 1) fps = 1; if (fps > 100) fps = 100;
    if (bright < 1) bright = 1; if (bright > 255) bright = 255;
    if (proxDiv < 1) proxDiv = 1;

    sel = tool_select(wantIp, wantEp, 5, "LAN866x Click demo (Thumbstick + Proximity -> RGB)");
    if (sel < 0) return 2;
    rcp_get_endpoints(eps, RCP_MAX_ENDPOINTS);
    memcpy(s_ip, eps[sel].ip, 4);
    printf("Target %u.%u.%u.%u, streaming RTP video to :%d at %d fps. Ctrl-C to stop.\n",
           s_ip[0], s_ip[1], s_ip[2], s_ip[3], RTP_PORT, fps);

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { printf("WSAStartup failed\n"); return 3; }
    s_rtp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_rtp == INVALID_SOCKET) { printf("socket failed\n"); return 3; }
    s_ssrc = (uint32_t)GetTickCount();
    signal(SIGINT, on_sigint);

    /* fast sensor I/O: don't stall the animation if a frame is lost */
    rcp_set_timeout_ms(250);
    rcp_set_retries(2);

    while (s_run) {
        uint16_t nx, ny, np;
        uint8_t r, g, b;
        int okT, okP;

        okT = thumbstick_read(&nx, &ny);
        if (okT) { tx = nx; ty = ny; }
        okP = proximity_read(&np);
        if (okP) prox = np;

        /* Left display: X -> red, Y -> green (invert to match stick motion) */
        r = scale((uint32_t)(ADC_MAX - tx), ADC_MAX, (uint32_t)bright);
        g = scale((uint32_t)(ADC_MAX - ty), ADC_MAX, (uint32_t)bright);
        b = (uint8_t)(bright / 6);
        fill_half(0, r, g, b);

        /* Right display: closer object -> warmer (blue far -> red near) */
        {
            uint8_t inten = scale((uint32_t)prox / (uint32_t)proxDiv, (uint32_t)bright, (uint32_t)bright);
            fill_half(10, inten, 0, (uint8_t)(bright - inten));
        }

        rtp_send();
        rcp_poll();

        printf("\r  Thumbstick x=%4u y=%4u %s | Proximity raw=%5u %s   ",
               tx, ty, okT ? "  " : "(!)", prox, okP ? "  " : "(!)");
        fflush(stdout);
        Sleep((DWORD)(1000 / fps));
    }

    /* clear both displays and release the peripherals */
    printf("\nStopping - clearing displays ...\n");
    memset(s_fb, 0, sizeof(s_fb));
    rtp_send(); Sleep(30); rtp_send();
    if (s_spi != UINT16_MAX) { CloseSpiVar_t c; c.HandleSpi = s_spi; rcp_close_spi(&c); }
    if (s_i2c != UINT16_MAX) { CloseI2CVar_t c; c.HandleI2C = s_i2c; rcp_close_i2c(&c); }
    closesocket(s_rtp); WSACleanup();
    return 0;
}
