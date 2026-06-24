/*
 * sys_cli.c - System/misc family of the bridge CLI: mirrors the host tools
 *   servicetest, boot, uart, video. Registered as the "sys" SYS_CMD group; type
 *   the name directly. 'video' streams a built-in animated test pattern as RTP
 *   to the endpoint displays (the host 'video' uses ffmpeg+a file, not portable);
 *   it is bounded ([secs]) + Ctrl-C/'q'. No C++.
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

static int chk_abort(SYS_CONSOLE_HANDLE con)
{
    char ch; int hit = 0;
    while (SYS_CONSOLE_Read(con, &ch, 1) > 0)
        if (ch == 0x03 || ch == 'q' || ch == 'Q') hit = 1;
    return hit;
}

/* ===================== servicetest ======================================= */
typedef struct { uint16_t id; const char *name; int unsafe; } method_t;
static const method_t METHODS[] = {
    { 0x1002, "GetStatus",          0 }, { 0x1600, "GetNetworkStatus",   0 },
    { 0x1003, "ReadDiagnosisData",  0 }, { 0x1601, "WakeupNetwork",      0 },
    { 0x1000, "Reboot",             1 }, { 0x1004, "StartUpdate",        1 },
    { 0x1005, "WriteImage",         1 }, { 0x1006, "FinishUpdate",       1 },
    { 0x1105, "ReleaseDigitalPins", 0 }, { 0x1300, "OpenGpio",           0 },
    { 0x1330, "SetGpio",            0 }, { 0x1332, "GetGpio",            0 },
    { 0x1200, "OpenI2C",            0 }, { 0x1204, "WriteI2C",           0 },
    { 0x1220, "ReadI2C",            0 }, { 0x1208, "WriteAndReadI2C",    0 },
    { 0x1202, "CloseI2C",           0 }, { 0x1500, "OpenSpi",            0 },
    { 0x1508, "WriteAndReadSpi",    0 }, { 0x1509, "WriteAndReadSpi2",   0 },
    { 0x1502, "CloseSpi",           0 }, { 0x1400, "OpenUart",           0 },
    { 0x1404, "WriteUart",          0 }, { 0x1420, "ReadUart",           0 },
    { 0x1700, "OpenAdc",            0 }, { 0x1720, "ReadAdc",            0 },
    { 0x1702, "CloseAdc",           0 }, { 0x1800, "OpenPwm",            0 },
    { 0x1804, "WritePwm",           0 }, { 0x1802, "ClosePwm",           0 },
};
static volatile int s_pdone; static volatile ReturnCode_t s_prc;
static void on_probe(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{ (void)ctx; (void)rx; (void)rxLen; s_prc = rc; s_pdone = 1; }
static ReturnCode_t probe(uint16_t method)
{
    int a;
    for (a = 0; a < 3; ++a) {
        s_pdone = 0; s_prc = RT_TIMEOUT;
        if (rcp_async_request(method, NULL, 0, on_probe, NULL) != RT_OK) { plat_sleep_ms(30); continue; }
        while (!s_pdone) { rcp_async_poll(); plat_sleep_ms(2); }
        if (s_prc != RT_TIMEOUT) return s_prc;
        plat_sleep_ms(20);
    }
    return RT_TIMEOUT;
}
static void cmd_servicetest(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    int unsafe = 0, i, nP = 0, nA = 0, nO = 0;
    (void)pCmdIO;
    if (argc >= 2 && strcmp(argv[1], "unsafe") == 0) unsafe = 1;
    if (!sel_first_ep()) { SYS_CONSOLE_PRINT("[servicetest] no endpoint - run 'discovery' first\r\n"); return; }
    rcp_set_async_timeout_ms(400);
    SYS_CONSOLE_PRINT("Probing %d RCP methods (empty payload); 0x03=absent, else present.%s\r\n",
                      (int)(sizeof(METHODS) / sizeof(METHODS[0])), unsafe ? "" : " (Reboot/Update skipped; 'servicetest unsafe' to include)");
    for (i = 0; i < (int)(sizeof(METHODS) / sizeof(METHODS[0])); ++i) {
        ReturnCode_t rc;
        if (METHODS[i].unsafe && !unsafe) { SYS_CONSOLE_PRINT("  %-20s 0x%04X  skipped\r\n", METHODS[i].name, METHODS[i].id); continue; }
        rc = probe(METHODS[i].id);
        SYS_CONSOLE_PRINT("  %-20s 0x%04X  rc=0x%04X  %s\r\n", METHODS[i].name, METHODS[i].id, (unsigned)rc,
            rc == RT_UNKNOWN_METHOD ? "ABSENT" : rc == RT_TIMEOUT ? "no response" : "PRESENT");
        if (rc == RT_UNKNOWN_METHOD) nA++; else if (rc == RT_TIMEOUT) nO++; else nP++;
        plat_sleep_ms(20);
    }
    rcp_set_async_timeout_ms(150);
    SYS_CONSOLE_PRINT("Summary: %d present, %d absent, %d no-response.\r\n", nP, nA, nO);
}

/* ===================== boot =============================================== */
static long long boot_uptime(void)
{
    GetStatusReply_t st; memset(&st, 0, sizeof(st));
    if (rcp_get_status(&st) == RT_OK) return (long long)(st.UpTime / 1000000000ULL);
    return -1;
}
static void boot_status(const char *when)
{
    GetStatusReply_t st; memset(&st, 0, sizeof(st));
    if (rcp_get_status(&st) == RT_OK)
        SYS_CONSOLE_PRINT("  [%s] app=%s chip=%s main=%s boot=%s up=%llus\r\n", when,
            st.ActiveApplication, st.ChipIdentifier, st.MainApplicationVersion,
            st.BootApplicationVersion, (unsigned long long)(st.UpTime / 1000000000ULL));
    else SYS_CONSOLE_PRINT("  [%s] GetStatus failed\r\n", when);
}
static int boot_wait_up(int waitS)
{
    uint32_t endt = plat_now_ms() + (uint32_t)waitS * 1000u;
    while ((int32_t)(plat_now_ms() - endt) < 0) {
        rcp_poll();                       /* drives SD re-discovery */
        plat_sleep_ms(100);
        if (sel_first_ep() && boot_uptime() >= 0) return 1;
    }
    return 0;
}
static int boot_reboot_to(const char *image, const char *label, int waitS)
{
    long long before, after; ReturnCode_t rc;
    before = boot_uptime();
    SYS_CONSOLE_PRINT("\r\nRebooting into %s ...\r\n", label);
    rcp_set_retries(0); rc = rcp_reboot(image); rcp_set_retries(3);
    SYS_CONSOLE_PRINT("  reboot rc=%d (%s)\r\n", rc, rc == RT_OK ? "acknowledged" : "no ack - may still reboot");
    plat_sleep_ms(2500);
    if (!boot_wait_up(waitS)) { SYS_CONSOLE_PRINT("  ! endpoint did not reappear within %d s\r\n", waitS); return 0; }
    after = boot_uptime();
    if (after >= 0 && (before < 0 || after < before))
        SYS_CONSOLE_PRINT("  -> REBOOTED (uptime %llds -> %llds)\r\n", before, after);
    else SYS_CONSOLE_PRINT("  -> did NOT reset (uptime %llds -> %llds)\r\n", before, after);
    return 1;
}
static void cmd_boot(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    const char *to = "cycle"; int waitS = 20;
    (void)pCmdIO;
    if (argc >= 2) to = argv[1];
    if (argc >= 3) waitS = (int)strtoul(argv[2], NULL, 10);
    if (waitS < 1) waitS = 1; if (waitS > 120) waitS = 120;
    if (!sel_first_ep()) { SYS_CONSOLE_PRINT("[boot] no endpoint - run 'discovery' first\r\n"); return; }
    boot_status("current");
    if (strcmp(to, "bootloader") == 0) {
        if (boot_reboot_to(RCP_IMAGE_BOOTLOADER, "bootloader", waitS)) boot_status("bootloader");
    } else if (strcmp(to, "main") == 0) {
        if (boot_reboot_to(RCP_IMAGE_MAIN, "main app", waitS)) boot_status("main app");
    } else {
        if (!boot_reboot_to(RCP_IMAGE_BOOTLOADER, "bootloader", waitS)) return;
        boot_status("bootloader");
        if (!boot_reboot_to(RCP_IMAGE_MAIN, "main app", waitS)) return;
        boot_status("main app (restored)");
    }
}

/* ===================== uart =============================================== */
static uint16_t unescape(const char *s, uint8_t *out, uint16_t cap)
{
    uint16_t n = 0;
    while (*s && n < cap) {
        if (*s == '\\' && s[1]) { ++s;
            out[n++] = (*s == 'r') ? '\r' : (*s == 'n') ? '\n' : (*s == 't') ? '\t' : (uint8_t)*s;
        } else out[n++] = (uint8_t)*s;
        ++s;
    }
    return n;
}
static void cmd_uart(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    OpenUartVar_t ov; OpenUartReply_t orep; uint8_t tx = 0u, rx = 3u; uint32_t baud = 115200u;
    const char *wstr = NULL;
    (void)pCmdIO;
    if (argc < 3) { SYS_CONSOLE_PRINT("Usage: uart <txpin> <rxpin> [baud] [text]  (\\r \\n escapes; reads once after)\r\n"); return; }
    tx = (uint8_t)strtoul(argv[1], NULL, 10);
    rx = (uint8_t)strtoul(argv[2], NULL, 10);
    if (argc >= 4) baud = (uint32_t)strtoul(argv[3], NULL, 10);
    if (argc >= 5) wstr = argv[4];

    if (!sel_first_ep()) { SYS_CONSOLE_PRINT("[uart] no endpoint - run 'discovery' first\r\n"); return; }
    rcp_set_timeout_ms(800); rcp_set_retries(2);
    { ReleaseDigitalPinsVar_t rel; memset(&rel, 0, sizeof(rel));
      rel.PinIdList[0] = tx; rel.PinIdList[1] = rx; rel.PinIdListLength = 2; rcp_release_digital_pins(&rel); }
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdTx = tx; ov.PinIdRx = rx; ov.PinIdRts = 0xFF; ov.PinIdCts = 0xFF;
    ov.Notification = 0u; ov.BaudRate = baud; ov.Parity = 2; ov.StopBits = 0; ov.BitOrder = 0;
    ov.RxBufferSize = 256; ov.RxThreshold = 1; ov.RxTimeout = 10;
    if (rcp_open_uart(&ov, &orep) != RT_OK) { SYS_CONSOLE_PRINT("OpenUart failed (UART not configured on this build?)\r\n"); goto restore; }
    SYS_CONSOLE_PRINT("UART open: TX=PA%02u RX=PA%02u @ %u Bd\r\n", (unsigned)tx, (unsigned)rx, (unsigned)baud);

    if (wstr) {
        WriteUartVar_t wv; memset(&wv, 0, sizeof(wv));
        wv.HandleUart = orep.HandleUart; wv.WriteId = 0;
        wv.WriteDataLength = unescape(wstr, wv.WriteData, sizeof(wv.WriteData));
        if (rcp_write_uart(&wv) == RT_OK) SYS_CONSOLE_PRINT("  TX %u byte(s)\r\n", (unsigned)wv.WriteDataLength);
        else SYS_CONSOLE_PRINT("WriteUart failed\r\n");
        plat_sleep_ms(50);
    }
    { ReadUartVar_t rv; ReadUartReply_t rr; uint16_t i;
      memset(&rv, 0, sizeof(rv)); memset(&rr, 0, sizeof(rr)); rv.HandleUart = orep.HandleUart;
      if (rcp_read_uart(&rv, &rr) == RT_OK) {
          if (rr.ReadDataLength) {
              SYS_CONSOLE_PRINT("  RX %u byte(s): \"", (unsigned)rr.ReadDataLength);
              for (i = 0; i < rr.ReadDataLength; ++i) SYS_CONSOLE_PRINT("%c", (rr.ReadData[i] >= 0x20 && rr.ReadData[i] < 0x7F) ? rr.ReadData[i] : '.');
              SYS_CONSOLE_PRINT("\"\r\n");
          } else SYS_CONSOLE_PRINT("  RX: (nothing buffered)\r\n");
      } else SYS_CONSOLE_PRINT("ReadUart failed\r\n");
    }
restore:
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

/* ===================== video (built-in animated test pattern) ============= */
#define VX 20
#define VY 10
static uint8_t  v_fb[VY][VX][3];
static uint8_t  v_pkt[64 + VY * 6 + VY * VX * 3];
static uint32_t v_seq, v_ssrc;
static void v_rx(plat_udp_t *s, const uint8_t ip[4], uint16_t port,
                 const uint8_t *buf, uint16_t len, void *tag)
{ (void)s; (void)ip; (void)port; (void)buf; (void)len; (void)tag; }
static uint16_t v_rtp_send(plat_udp_t *s, const uint8_t ip[4])
{
    int n = 0, x, y; uint32_t ts = v_seq * 1000u;
    v_pkt[n++] = 0x80; v_pkt[n++] = (uint8_t)(0x80u | 96u);
    v_pkt[n++] = (uint8_t)(v_seq >> 8); v_pkt[n++] = (uint8_t)v_seq;
    v_pkt[n++] = (uint8_t)(ts >> 24); v_pkt[n++] = (uint8_t)(ts >> 16); v_pkt[n++] = (uint8_t)(ts >> 8); v_pkt[n++] = (uint8_t)ts;
    v_pkt[n++] = (uint8_t)(v_ssrc >> 24); v_pkt[n++] = (uint8_t)(v_ssrc >> 16); v_pkt[n++] = (uint8_t)(v_ssrc >> 8); v_pkt[n++] = (uint8_t)v_ssrc;
    v_pkt[n++] = (uint8_t)(v_seq >> 24); v_pkt[n++] = (uint8_t)(v_seq >> 16);
    for (y = 0; y < VY; ++y) {
        int cont = (y != (VY - 1)); uint16_t len = (uint16_t)(VX * 3);
        v_pkt[n++] = (uint8_t)(len >> 8); v_pkt[n++] = (uint8_t)len;
        v_pkt[n++] = (uint8_t)(y >> 8);   v_pkt[n++] = (uint8_t)y;
        v_pkt[n++] = (uint8_t)(cont ? 0x80 : 0x00); v_pkt[n++] = 0u;
    }
    for (y = 0; y < VY; ++y) for (x = 0; x < VX; ++x) {
        v_pkt[n++] = v_fb[y][x][0]; v_pkt[n++] = v_fb[y][x][1]; v_pkt[n++] = v_fb[y][x][2];
    }
    plat_udp_send(s, ip, 5001u, v_pkt, (uint16_t)n);
    v_seq++;
    return (uint16_t)n;   /* bytes sent (RTP UDP payload) - for the bandwidth tally */
}
static void cmd_video(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    rcp_endpoint_t eps[RCP_MAX_ENDPOINTS]; uint8_t ip[4]; plat_udp_t *s; uint16_t port = 0u;
    uint32_t secs = 0u, fps = 25u, bright = 160u, start, endt, step, tick = 0u, total = 0u;
    SYS_CONSOLE_HANDLE con = SYS_CONSOLE_HandleGet(SYS_CONSOLE_INDEX_0);
    int aborted = 0, x, y;
    (void)pCmdIO;
    if (argc >= 2) secs   = (uint32_t)strtoul(argv[1], NULL, 10);
    if (argc >= 3) fps    = (uint32_t)strtoul(argv[2], NULL, 10);
    if (argc >= 4) bright = (uint32_t)strtoul(argv[3], NULL, 10);
    if (secs > 3600u) secs = 3600u;   /* 0 (default) = run until Ctrl-C / 'q' */
    if (fps < 1u) fps = 1u; if (fps > 60u) fps = 60u;
    if (bright > 255u) bright = 255u;
    step = 1000u / fps;

    if (!sel_first_ep() || rcp_get_endpoints(eps, RCP_MAX_ENDPOINTS) == 0u) {
        SYS_CONSOLE_PRINT("[video] no endpoint - run 'discovery' first\r\n"); return;
    }
    memcpy(ip, eps[0].ip, 4);
    s = plat_udp_open(&port, v_rx, NULL);
    if (!s) { SYS_CONSOLE_PRINT("[video] RTP socket failed\r\n"); return; }
    v_seq = 0u; v_ssrc = plat_now_ms();

    if (secs == 0u)
        SYS_CONSOLE_PRINT("[video] built-in test pattern -> %u.%u.%u.%u RTP :5001, %u fps, runs until Ctrl-C or 'q'...\r\n",
                          ip[0], ip[1], ip[2], ip[3], (unsigned)fps);
    else
        SYS_CONSOLE_PRINT("[video] built-in test pattern -> %u.%u.%u.%u RTP :5001, %u fps for %u s ('q' to stop)...\r\n",
                          ip[0], ip[1], ip[2], ip[3], (unsigned)fps, (unsigned)secs);
    start = plat_now_ms(); endt = start + secs * 1000u;
    while (!aborted && (secs == 0u || (int32_t)(plat_now_ms() - endt) < 0)) {
        uint32_t ph = tick;                       /* animate: sweeping rainbow columns */
        for (y = 0; y < VY; ++y) for (x = 0; x < VX; ++x) {
            uint32_t hue = (uint32_t)((x * 13u + y * 7u + ph * 6u)) % 360u;
            uint32_t r, g, b, hh = hue / 60u, f = (hue % 60u) * 255u / 60u;
            switch (hh) {
                case 0:  r = 255; g = f;   b = 0;   break;
                case 1:  r = 255 - f; g = 255; b = 0; break;
                case 2:  r = 0; g = 255; b = f;   break;
                case 3:  r = 0; g = 255 - f; b = 255; break;
                case 4:  r = f; g = 0; b = 255;   break;
                default: r = 255; g = 0; b = 255 - f; break;
            }
            v_fb[y][x][0] = (uint8_t)(r * bright / 255u);
            v_fb[y][x][1] = (uint8_t)(g * bright / 255u);
            v_fb[y][x][2] = (uint8_t)(b * bright / 255u);
        }
        total += v_rtp_send(s, ip);
        tick++;
        if (chk_abort(con)) aborted = 1;
        plat_sleep_ms(step);
    }
    {   /* bandwidth tally over the whole run (RTP UDP payload sent) */
        uint32_t el = plat_now_ms() - start; if (el == 0u) el = 1u;
        SYS_CONSOLE_PRINT("\r\n[video] %u frames, %u bytes in %u ms -> %u fps, ~%u kbit/s (RTP UDP payload)\r\n",
            (unsigned)tick, (unsigned)total, (unsigned)el,
            (unsigned)(((uint64_t)tick * 1000u) / el),
            (unsigned)(((uint64_t)total * 8u) / el));
    }
    memset(v_fb, 0, sizeof(v_fb)); v_rtp_send(s, ip); plat_sleep_ms(30); v_rtp_send(s, ip);
    plat_udp_close(s);
    SYS_CONSOLE_PRINT("[video] stopped, displays cleared.\r\n");
}

static const SYS_CMD_DESCRIPTOR sys_cmd_tbl[] = {
    {"servicetest", (SYS_CMD_FNC) cmd_servicetest, ": probe which RCP methods exist (servicetest [unsafe])"},
    {"boot",        (SYS_CMD_FNC) cmd_boot,        ": reboot endpoint app<->bootloader (boot [cycle|bootloader|main] [waitS])"},
    {"uart",        (SYS_CMD_FNC) cmd_uart,        ": open a UART, write/read (uart <tx> <rx> [baud] [text])"},
    {"video",       (SYS_CMD_FNC) cmd_video,       ": stream a built-in RTP test pattern (video [secs] [fps] [bright])"},
};

void SYS_CLI_Init(void)
{
    SYS_CMD_ADDGRP(sys_cmd_tbl, sizeof(sys_cmd_tbl) / sizeof(*sys_cmd_tbl), "sys", ": system/misc demos");
}
