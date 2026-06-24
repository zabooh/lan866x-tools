/*
 * i2c_cli.c - I2C family of the bridge CLI: mirrors the host tools
 *   i2cscan, i2cid, proxmon, lan8680, proxled. Registered as the "i2c" SYS_CMD
 *   group; type the name directly. Monitors are bounded ([secs]) and abortable
 *   with Ctrl-C / 'q'. Default I2C pins SDA=PA08/SCL=PA09 (the Click slot-3
 *   default), like clickdemo. No C++.
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

#define I2C_SDA 8u
#define I2C_SCL 9u

#define VCNL4200_ADDR     0x51u
#define VCNL4200_ID_REG   0x0Eu
#define VCNL4200_ID_VALUE 0x1058u
#define VCNL4200_PS_CONF1 0x03u
#define VCNL4200_PS_CONF3 0x04u
#define VCNL4200_ALS_CONF 0x00u
#define VCNL4200_PS_DATA  0x08u

static int chk_abort(SYS_CONSOLE_HANDLE con)
{
    char ch; int hit = 0;
    while (SYS_CONSOLE_Read(con, &ch, 1) > 0)
        if (ch == 0x03 || ch == 'q' || ch == 'Q') hit = 1;
    return hit;
}

static int i2c_open(uint8_t sda, uint8_t scl, uint8_t speed, uint16_t *h)
{
    ReleaseDigitalPinsVar_t rel; OpenI2CVar_t ov; OpenI2CReply_t orep;
    memset(&rel, 0, sizeof(rel)); rel.PinIdList[0] = sda; rel.PinIdList[1] = scl; rel.PinIdListLength = 2;
    rcp_release_digital_pins(&rel); plat_sleep_ms(25);
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdSda = sda; ov.PinIdScl = scl; ov.ClockSpeed = speed;
    if (rcp_open_i2c(&ov, &orep) != RT_OK) return 0;
    *h = orep.HandleI2C; return 1;
}
static void i2c_close(uint16_t h) { CloseI2CVar_t c; c.HandleI2C = h; rcp_close_i2c(&c); }

/* blocking 16-bit register read (write reg addr, repeated-start read 2 bytes) */
static int i2c_rd16(uint16_t h, uint16_t addr, uint8_t reg, int msbFirst, uint16_t *out)
{
    WriteAndReadI2CVar_t rq; ReadI2CReply_t rp;
    memset(&rq, 0, sizeof(rq)); memset(&rp, 0, sizeof(rp));
    rq.HandleI2C = h; rq.DeviceAddress = addr; rq.ReadDataLength = 2;
    rq.WriteId = 0; rq.WriteDataLength = 1; rq.WriteData[0] = reg;
    if (rcp_write_and_read_i2c(&rq, &rp) != RT_OK || rp.ReadDataLength < 2) return 0;
    *out = msbFirst ? (uint16_t)((rp.ReadData[0] << 8) | rp.ReadData[1])
                    : (uint16_t)(rp.ReadData[0] | (rp.ReadData[1] << 8));
    return 1;
}

static int vcnl_cfg(uint16_t h, uint8_t reg, uint8_t lo, uint8_t hi)
{
    WriteI2CVar_t w; memset(&w, 0, sizeof(w));
    w.HandleI2C = h; w.DeviceAddress = VCNL4200_ADDR; w.WriteId = 0;
    w.WriteData[0] = reg; w.WriteData[1] = lo; w.WriteData[2] = hi; w.WriteDataLength = 3;
    return rcp_write_i2c(&w) == RT_OK;
}
static int vcnl_init(uint16_t h)
{
    uint16_t id = 0;
    if (!i2c_rd16(h, VCNL4200_ADDR, VCNL4200_ID_REG, 0, &id) || id != VCNL4200_ID_VALUE) return 0;
    plat_sleep_ms(25); if (!vcnl_cfg(h, VCNL4200_PS_CONF1, 0x02, 0x08)) return 0;
    plat_sleep_ms(25); if (!vcnl_cfg(h, VCNL4200_PS_CONF3, 0x01, 0x07)) return 0;
    plat_sleep_ms(25); if (!vcnl_cfg(h, VCNL4200_ALS_CONF, 0x00, 0x00)) return 0;
    return 1;
}

/* --- i2cscan [sda] [scl] [speed] (mirrors i2cscan.c) ----------------------- */
static void cmd_i2cscan(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    uint8_t sda = I2C_SDA, scl = I2C_SCL, speed = 1u;
    uint16_t h; int i, found = 0;
    (void)pCmdIO;
    if (argc >= 2) sda   = (uint8_t)strtoul(argv[1], NULL, 10);
    if (argc >= 3) scl   = (uint8_t)strtoul(argv[2], NULL, 10);
    if (argc >= 4) speed = (uint8_t)strtoul(argv[3], NULL, 10);

    if (!sel_first_ep()) { SYS_CONSOLE_PRINT("[i2cscan] no endpoint - run 'discovery' first\r\n"); return; }
    if (!i2c_open(sda, scl, speed, &h)) { SYS_CONSOLE_PRINT("OpenI2C failed (pins/speed?)\r\n"); return; }
    rcp_set_timeout_ms(150); rcp_set_retries(3);

    SYS_CONSOLE_PRINT("\r\nScanning (SDA=PA%02u SCL=PA%02u):\r\n", (unsigned)sda, (unsigned)scl);
    SYS_CONSOLE_PRINT("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (i = 0; i < 0x80; ++i) {
        if ((i & 0x0F) == 0) SYS_CONSOLE_PRINT("%02x:", i);
        if (i >= 0x08 && i <= 0x77) {
            ReadI2CVar_t rv; ReadI2CReply_t rr;
            memset(&rv, 0, sizeof(rv)); memset(&rr, 0, sizeof(rr));
            rv.HandleI2C = h; rv.DeviceAddress = (uint16_t)i; rv.ReadDataLength = 1;
            if (rcp_read_i2c(&rv, &rr) == RT_OK) { SYS_CONSOLE_PRINT(" %02x", i); found++; }
            else SYS_CONSOLE_PRINT(" --");
        } else SYS_CONSOLE_PRINT("   ");
        if ((i & 0x0F) == 0x0F) SYS_CONSOLE_PRINT("\r\n");
    }
    SYS_CONSOLE_PRINT("%d device(s) found.\r\n", found);
    i2c_close(h);
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

/* --- i2cid [addr] [reg] [sda] [scl] [speed] (mirrors i2cid.c) -------------- */
static void cmd_i2cid(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    uint16_t addr = VCNL4200_ADDR; uint8_t reg = VCNL4200_ID_REG, sda = I2C_SDA, scl = I2C_SCL, speed = 1u;
    uint16_t h, id = 0;
    (void)pCmdIO;
    if (argc >= 2) addr  = (uint16_t)strtoul(argv[1], NULL, 0);
    if (argc >= 3) reg   = (uint8_t)strtoul(argv[2], NULL, 0);
    if (argc >= 4) sda   = (uint8_t)strtoul(argv[3], NULL, 10);
    if (argc >= 5) scl   = (uint8_t)strtoul(argv[4], NULL, 10);
    if (argc >= 6) speed = (uint8_t)strtoul(argv[5], NULL, 10);

    if (!sel_first_ep()) { SYS_CONSOLE_PRINT("[i2cid] no endpoint - run 'discovery' first\r\n"); return; }
    if (!i2c_open(sda, scl, speed, &h)) { SYS_CONSOLE_PRINT("OpenI2C failed\r\n"); return; }
    rcp_set_timeout_ms(300); rcp_set_retries(3);

    if (i2c_rd16(h, addr, reg, 0, &id)) {     /* VCNL4200 = LSB first */
        SYS_CONSOLE_PRINT("ID @0x%02X reg 0x%02X = 0x%04X\r\n", (unsigned)addr, (unsigned)reg, id);
        if (addr == VCNL4200_ADDR && reg == VCNL4200_ID_REG)
            SYS_CONSOLE_PRINT("  -> %s\r\n", id == VCNL4200_ID_VALUE ?
                "VCNL4200 (Proximity 3 Click) detected." : "unexpected ID for a VCNL4200.");
    } else {
        SYS_CONSOLE_PRINT("read failed - device present / I2C routed?\r\n");
    }
    i2c_close(h);
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

/* --- proxmon [secs] [sda] [scl] (mirrors proxmon.c) ------------------------ */
static void cmd_proxmon(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    uint8_t sda = I2C_SDA, scl = I2C_SCL;
    uint32_t secs = 20u, start, endt, lastP = 0u;
    uint16_t h;
    SYS_CONSOLE_HANDLE con = SYS_CONSOLE_HandleGet(SYS_CONSOLE_INDEX_0);
    int aborted = 0;
    (void)pCmdIO;
    if (argc >= 2) secs = (uint32_t)strtoul(argv[1], NULL, 10);
    if (argc >= 3) sda  = (uint8_t)strtoul(argv[2], NULL, 10);
    if (argc >= 4) scl  = (uint8_t)strtoul(argv[3], NULL, 10);
    if (secs < 1u) secs = 1u; if (secs > 600u) secs = 600u;

    if (!sel_first_ep()) { SYS_CONSOLE_PRINT("[proxmon] no endpoint - run 'discovery' first\r\n"); return; }
    rcp_set_timeout_ms(400); rcp_set_retries(2);
    if (!i2c_open(sda, scl, 1u, &h)) { SYS_CONSOLE_PRINT("OpenI2C failed\r\n"); goto restore; }
    if (!vcnl_init(h)) { SYS_CONSOLE_PRINT("VCNL4200 not found / init failed\r\n"); i2c_close(h); goto restore; }

    SYS_CONSOLE_PRINT("Proximity monitor for %u s (move your hand; 'q' to stop)...\r\n", (unsigned)secs);
    start = plat_now_ms(); endt = start + secs * 1000u;
    while (!aborted && (int32_t)(plat_now_ms() - endt) < 0) {
        uint16_t raw = 0;
        if (i2c_rd16(h, VCNL4200_ADDR, VCNL4200_PS_DATA, 0, &raw)) {
            uint32_t now = plat_now_ms();
            if (now - lastP >= 150u) { SYS_CONSOLE_PRINT("\r  proximity raw = %5u   ", raw); lastP = now; }
        }
        if (chk_abort(con)) aborted = 1;
        plat_sleep_ms(50);
    }
    SYS_CONSOLE_PRINT("\r\nDone.\r\n");
    i2c_close(h);
restore:
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

/* --- lan8680 [sda] [scl] : read the LAN8680 SBC, READ-ONLY (lan8680.c) ------ */
#define L8680_PHY_ID1 0x40u
#define L8680_PHY_ID2 0x41u
#define L8680_STS0    0x43u
#define L8680_MODEL   0x20u
static const uint8_t L8680_ADDRS[] = { 0x20u, 0x40u };
static const uint8_t L8680_CAND[][2] = { {8,9}, {12,13}, {0,1}, {4,5} };
static const char *L8680_NAME[] = { "SER2", "SER3", "SER0", "SER1" };

static uint8_t l8680_find(uint16_t h)
{
    uint16_t id2; unsigned i;
    for (i = 0; i < sizeof(L8680_ADDRS); ++i)
        if (i2c_rd16(h, L8680_ADDRS[i], L8680_PHY_ID2, 1, &id2) && ((id2 >> 4) & 0x3Fu) == L8680_MODEL)
            return L8680_ADDRS[i];
    return 0u;
}
static void l8680_sts0(uint16_t s)
{
    SYS_CONSOLE_PRINT("  STS0 = 0x%04X", s);
    if ((s & 0x07FFu) == 0u) { SYS_CONSOLE_PRINT("  (supplies & temperature nominal)\r\n"); return; }
    SYS_CONSOLE_PRINT("  warnings:");
    if (s & (1u<<9)) SYS_CONSOLE_PRINT(" OTWR"); if (s & (1u<<8)) SYS_CONSOLE_PRINT(" BOVW");
    if (s & (1u<<7)) SYS_CONSOLE_PRINT(" BUVW"); if (s & (1u<<6)) SYS_CONSOLE_PRINT(" VSOVW");
    if (s & (1u<<5)) SYS_CONSOLE_PRINT(" VSEN33OVSD"); if (s & (1u<<4)) SYS_CONSOLE_PRINT(" VSEN18OVSD");
    if (s & (1u<<3)) SYS_CONSOLE_PRINT(" VSEN33UVW"); if (s & (1u<<2)) SYS_CONSOLE_PRINT(" VSEN18UVW");
    if (s & (1u<<1)) SYS_CONSOLE_PRINT(" VUCOVR"); if (s & (1u<<0)) SYS_CONSOLE_PRINT(" VUCUVF");
    SYS_CONSOLE_PRINT("\r\n");
}
static void cmd_lan8680(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    uint16_t h, id1 = 0, id2 = 0, sts = 0; uint8_t addr = 0;
    int useOne = 0, sda = 8, scl = 9, ci = -1, i;
    (void)pCmdIO;
    if (argc >= 3) { useOne = 1; sda = (int)strtoul(argv[1], NULL, 10); scl = (int)strtoul(argv[2], NULL, 10); }

    if (!sel_first_ep()) { SYS_CONSOLE_PRINT("[lan8680] no endpoint - run 'discovery' first\r\n"); return; }
    rcp_set_timeout_ms(200); rcp_set_retries(1);

    if (useOne) {
        if (!i2c_open((uint8_t)sda, (uint8_t)scl, 0u, &h)) { SYS_CONSOLE_PRINT("OpenI2C failed on PA%02d/PA%02d\r\n", sda, scl); goto restore; }
        addr = l8680_find(h);
        if (!addr) { SYS_CONSOLE_PRINT("No LAN8680 on PA%02d/PA%02d\r\n", sda, scl); i2c_close(h); goto restore; }
    } else {
        SYS_CONSOLE_PRINT("Probing SERCOM I2C buses for the LAN8680 (0x20/0x40)...\r\n");
        for (i = 0; i < (int)(sizeof(L8680_CAND)/sizeof(L8680_CAND[0])); ++i) {
            int s = L8680_CAND[i][0], c = L8680_CAND[i][1];
            SYS_CONSOLE_PRINT("  %-4s PA%02d/PA%02d ... ", L8680_NAME[i], s, c);
            if (!i2c_open((uint8_t)s, (uint8_t)c, 0u, &h)) { SYS_CONSOLE_PRINT("OpenI2C failed\r\n"); continue; }
            addr = l8680_find(h);
            if (addr) { SYS_CONSOLE_PRINT("FOUND @ 0x%02X\r\n", addr); sda = s; scl = c; ci = i; break; }
            SYS_CONSOLE_PRINT("no\r\n"); i2c_close(h);
        }
        if (!addr) { SYS_CONSOLE_PRINT("LAN8680 not found on any probeable bus.\r\n"); goto restore; }
        (void)ci;
    }

    i2c_rd16(h, addr, L8680_PHY_ID1, 1, &id1);
    i2c_rd16(h, addr, L8680_PHY_ID2, 1, &id2);
    SYS_CONSOLE_PRINT("\r\nLAN8680 @ PA%02d/PA%02d addr 0x%02X:\r\n", sda, scl, addr);
    SYS_CONSOLE_PRINT("  PHY_ID1=0x%04X PHY_ID2=0x%04X (MODEL=0x%02X REV=%u)\r\n",
                      id1, id2, (unsigned)((id2 >> 4) & 0x3Fu), (unsigned)(id2 & 0x0Fu));
    SYS_CONSOLE_PRINT("  -> %s\r\n", ((id2 >> 4) & 0x3Fu) == L8680_MODEL ?
        "LAN8680 SBC confirmed." : "unexpected MODEL.");
    if (i2c_rd16(h, addr, L8680_STS0, 1, &sts)) l8680_sts0(sts);
    i2c_close(h);
restore:
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

/* --- proxled [max] [secs] : proximity drives LD1..LD3 (proxled.c) ---------- */
static volatile uint16_t s_pl_prox; static volatile int s_pl_pending;
static uint32_t s_pl_wid;
static void on_pl(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{
    (void)ctx;
    if (rc == RT_OK && rx) { uint8_t d[8] = {0}; uint16_t l = sizeof(d);
        if (rcp_dec_i2c_read(rx, rxLen, d, &l) && l >= 2) s_pl_prox = (uint16_t)(d[0] | (d[1] << 8)); }
    s_pl_pending = 0;
}
static void cmd_proxled(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    static const uint8_t LEDS[3] = { 2u, 6u, 10u };
    uint16_t h, ledH[3]; uint32_t maxRaw = 400u, secs = 30u, start, endt;
    int level = -1, i, aborted = 0;
    SYS_CONSOLE_HANDLE con = SYS_CONSOLE_HandleGet(SYS_CONSOLE_INDEX_0);
    (void)pCmdIO;
    if (argc >= 2) maxRaw = (uint32_t)strtoul(argv[1], NULL, 10);
    if (argc >= 3) secs   = (uint32_t)strtoul(argv[2], NULL, 10);
    if (maxRaw < 1u) maxRaw = 1u;
    if (secs < 1u) secs = 1u; if (secs > 600u) secs = 600u;

    if (!sel_first_ep()) { SYS_CONSOLE_PRINT("[proxled] no endpoint - run 'discovery' first\r\n"); return; }
    rcp_set_timeout_ms(400); rcp_set_retries(2);
    if (!i2c_open(I2C_SDA, I2C_SCL, 1u, &h)) { SYS_CONSOLE_PRINT("OpenI2C failed\r\n"); goto restore; }
    if (!vcnl_init(h)) { SYS_CONSOLE_PRINT("VCNL4200 not found / init failed\r\n"); i2c_close(h); goto restore; }
    for (i = 0; i < 3; ++i) {
        ReleaseDigitalPinsVar_t rel; OpenGpioVar_t gv; OpenGpioReply_t gr;
        memset(&rel, 0, sizeof(rel)); rel.PinIdList[0] = LEDS[i]; rel.PinIdListLength = 1;
        rcp_release_digital_pins(&rel); plat_sleep_ms(15);
        memset(&gv, 0, sizeof(gv)); memset(&gr, 0, sizeof(gr));
        gv.PinIdGpio = LEDS[i]; gv.Direction = 1;
        if (rcp_open_gpio(&gv, &gr) != RT_OK) { SYS_CONSOLE_PRINT("OpenGpio failed PA%02u\r\n", (unsigned)LEDS[i]); i2c_close(h); goto restore; }
        ledH[i] = gr.HandleGpio; plat_sleep_ms(15);
    }

    s_pl_prox = 0; s_pl_pending = 0; s_pl_wid = 0u;
    rcp_set_async_timeout_ms(150);
    SYS_CONSOLE_PRINT("Proximity -> LED level meter for %u s (hand closer = more LEDs; 'q' to stop)...\r\n", (unsigned)secs);
    start = plat_now_ms(); endt = start + secs * 1000u;
    while (!aborted && (int32_t)(plat_now_ms() - endt) < 0) {
        if (!s_pl_pending) {
            uint8_t pp[64], reg = VCNL4200_PS_DATA; uint16_t n;
            n = rcp_enc_i2c_read(pp, sizeof(pp), h, VCNL4200_ADDR, s_pl_wid++, &reg, 1, 2);
            if (n && rcp_async_request(0x1208u, pp, n, on_pl, NULL) == RT_OK) s_pl_pending = 1;
        }
        rcp_async_poll();
        { int v = s_pl_prox, lvl = (int)((uint32_t)v * 4u / maxRaw);
          if (lvl > 3) lvl = 3; if (lvl < 0) lvl = 0;
          if (lvl != level) {
              for (i = 0; i < 3; ++i) led_set(ledH[i], i < lvl);
              level = lvl;
              SYS_CONSOLE_PRINT("\r  raw=%5u level=%d/3 [%c%c%c]   ", s_pl_prox, lvl,
                  lvl > 0 ? '#' : '-', lvl > 1 ? '#' : '-', lvl > 2 ? '#' : '-');
          } }
        if (chk_abort(con)) aborted = 1;
        plat_sleep_ms(5);
    }
    for (i = 0; i < 3; ++i) led_set(ledH[i], 0);
    rcp_set_async_timeout_ms(150);
    SYS_CONSOLE_PRINT("\r\nStopped. LEDs off.\r\n");
    i2c_close(h);
restore:
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

static const SYS_CMD_DESCRIPTOR i2c_cmd_tbl[] = {
    {"i2cscan", (SYS_CMD_FNC) cmd_i2cscan, ": scan the I2C bus (i2cscan [sda] [scl] [speed])"},
    {"i2cid",   (SYS_CMD_FNC) cmd_i2cid,   ": read a device ID (i2cid [addr] [reg] [sda] [scl] [speed])"},
    {"proxmon", (SYS_CMD_FNC) cmd_proxmon, ": monitor VCNL4200 proximity (proxmon [secs] [sda] [scl])"},
    {"lan8680", (SYS_CMD_FNC) cmd_lan8680, ": read the LAN8680 SBC, read-only (lan8680 [sda] [scl])"},
    {"proxled", (SYS_CMD_FNC) cmd_proxled, ": proximity drives LD1..LD3 (proxled [max] [secs])"},
};

void I2C_CLI_Init(void)
{
    SYS_CMD_ADDGRP(i2c_cmd_tbl, sizeof(i2c_cmd_tbl) / sizeof(*i2c_cmd_tbl), "i2c", ": I2C demos");
}
