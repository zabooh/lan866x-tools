/*
 * lan8680.c  -  Read the LAN8680 T1S front-end (System Basis Chip) over I2C, via
 *               SOME/IP (RCP). Pure C. READ-ONLY.
 *
 * The LAN8680 SBC exposes its System Basis control/status registers over an I2C
 * SERVICING interface (slave-only) at the fixed 7-bit address 0x40. Registers are
 * 16-bit, MSB first. A register read = write the 1-byte register address, then a
 * repeated-START read of 2 bytes - which maps directly onto RCP WriteAndReadI2C.
 *
 * The LAN8680 lives on the board's "housekeeping" I2C bus (SDA-R/SCL-R), which is
 * one of the MCU's four SERCOMs. This tool auto-probes the SERCOM I2C pin pairs to
 * find which one the LAN8680 answers on, then reads & decodes its identity (PHY_ID1
 * /PHY_ID2) and, with --status, its SBC Status 0 register.
 *
 *   !! WRITE IS NOT IMPLEMENTED ON PURPOSE. The LAN8680 is the board's power supply
 *      + watchdog + reset controller; writing its registers can reset or power down
 *      the board. This tool only ever reads.
 *
 *   !! Auto-probing OpenI2Cs on SERCOMs that may currently drive the WS2812 displays
 *      (SER0/SER1) or the SPI thumbstick (SER3); that can briefly glitch them. The
 *      pins are released afterwards. Pass --sda/--scl to probe one known bus only.
 *
 * Usage:
 *   lan866x-lan8680                       auto-probe, identify the LAN8680
 *   lan866x-lan8680 --ip 192.168.0.54 --status
 *   lan866x-lan8680 --sda 4 --scl 5       probe only SER1 (PA04/PA05)
 *   lan866x-lan8680 --reg 0x43            read one 16-bit register and print it
 */
#include <stdlib.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

#define LAN8680_ADDR   0x40u
#define REG_PHY_ID1    0x40u
#define REG_PHY_ID2    0x41u
#define REG_STS0       0x43u
#define MODEL_LAN8680  0x20u   /* PHY_ID2 MODEL[5:0] = 100000b */

/* candidate SERCOM I2C pin pairs (P0=SDA, P1=SCL). SER2 (the Click application
 * I2C) is probed first because it is the least disruptive. */
static const int CAND[][2] = { {8,9}, {0,1}, {4,5}, {12,13} };
static const char *CAND_NAME[] = { "SER2", "SER0", "SER1", "SER3" };

/* Read one 16-bit LAN8680 register (MSB first). Returns 1 on success. */
static int rd16(uint16_t handle, uint8_t reg, uint16_t *out)
{
    WriteAndReadI2CVar_t rq; ReadI2CReply_t rp;
    memset(&rq, 0, sizeof(rq)); memset(&rp, 0, sizeof(rp));
    rq.HandleI2C = handle; rq.DeviceAddress = LAN8680_ADDR; rq.ReadDataLength = 2;
    rq.WriteId = 0; rq.WriteDataLength = 1; rq.WriteData[0] = reg;
    if (rcp_write_and_read_i2c(&rq, &rp) != RT_OK || rp.ReadDataLength < 2) return 0;
    *out = (uint16_t)((rp.ReadData[0] << 8) | rp.ReadData[1]);   /* MSB first */
    return 1;
}

/* Open an I2C bus on the given pins. Returns 1 + handle on success. */
static int open_bus(int sda, int scl, uint16_t *handle)
{
    ReleaseDigitalPinsVar_t rel; OpenI2CVar_t ov; OpenI2CReply_t orep;
    memset(&rel, 0, sizeof(rel));
    rel.PinIdList[0] = (uint8_t)sda; rel.PinIdList[1] = (uint8_t)scl; rel.PinIdListLength = 2;
    rcp_release_digital_pins(&rel); Sleep(25);
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdSda = (uint8_t)sda; ov.PinIdScl = (uint8_t)scl; ov.ClockSpeed = 0; /* 100 kHz */
    if (rcp_open_i2c(&ov, &orep) != RT_OK) return 0;
    *handle = orep.HandleI2C;
    return 1;
}

static void close_bus(uint16_t handle, int sda, int scl)
{
    CloseI2CVar_t c; ReleaseDigitalPinsVar_t rel;
    c.HandleI2C = handle; rcp_close_i2c(&c);
    memset(&rel, 0, sizeof(rel));
    rel.PinIdList[0] = (uint8_t)sda; rel.PinIdList[1] = (uint8_t)scl; rel.PinIdListLength = 2;
    rcp_release_digital_pins(&rel); Sleep(15);
}

/* Does a LAN8680 answer on this bus? (read PHY_ID2, check MODEL field) */
static int is_lan8680(uint16_t handle)
{
    uint16_t id2;
    if (!rd16(handle, REG_PHY_ID2, &id2)) return 0;
    return (((id2 >> 4) & 0x3Fu) == MODEL_LAN8680);
}

static void decode_sts0(uint16_t s)
{
    printf("  STS0 = 0x%04X  (warnings):\n", s);
    if (s & (1u << 9)) printf("    - over-temperature warning (OTWR)\n");
    if (s & (1u << 8)) printf("    - battery over-voltage warning (BOVW)\n");
    if (s & (1u << 7)) printf("    - battery under-voltage warning (BUVW)\n");
    if (s & (1u << 6)) printf("    - VS over-voltage warning (VSOVW)\n");
    if (s & (1u << 5)) printf("    - VSEN 3.3V over-voltage shutdown (VSEN33OVSD)\n");
    if (s & (1u << 4)) printf("    - VSEN 1.8V over-voltage shutdown (VSEN18OVSD)\n");
    if (s & (1u << 3)) printf("    - VSEN 3.3V under-voltage warning (VSEN33UVW)\n");
    if (s & (1u << 2)) printf("    - VSEN 1.8V under-voltage warning (VSEN18UVW)\n");
    if (s & (1u << 1)) printf("    - VUC over-voltage warning (VUCOVR)\n");
    if (s & (1u << 0)) printf("    - VUC under-voltage warning (VUCUVF)\n");
    if ((s & 0x07FFu) == 0) printf("    (none set - supplies & temperature nominal)\n");
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, sda = -1, scl = -1, status = 0;
    long reg = -1;
    uint16_t handle = 0; int foundSda = -1, foundScl = -1; const char *foundName = "?";

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-lan8680 - read the LAN8680 SBC over I2C, READ-ONLY (pure C)\n"
                   "  (auto-probes the SERCOM I2C buses for the LAN8680 @ 0x40)\n"
                   "  --sda <n> --scl <n>   probe only this one bus (PA index)\n"
                   "  --status              also read & decode SBC Status 0 (0x43)\n"
                   "  --reg <r>             read one 16-bit register and print it (hex ok)\n"
                   "  --ip/--ep             target endpoint\n"
                   "WRITE is intentionally not supported (the LAN8680 is the board's power/\n"
                   "watchdog/reset controller - writing it can reset or power down the board).\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")     && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")       && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sda")      && i+1<argc) sda = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scl")      && i+1<argc) scl = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--status")) status = 1;
        else if (!strcmp(argv[i], "--reg")      && i+1<argc) reg = strtol(argv[++i], NULL, 0);
    }

    if (tool_select(wantIp, wantEp, 5, "LAN866x LAN8680 SBC reader (read-only, pure C)") < 0) return 2;

    rcp_set_timeout_ms(200); rcp_set_retries(1);   /* fast probing: absent addr never replies */

    if (sda >= 0 && scl >= 0) {
        if (!open_bus(sda, scl, &handle)) { printf("OpenI2C failed on PA%02d/PA%02d.\n", sda, scl); return 3; }
        if (is_lan8680(handle)) { foundSda = sda; foundScl = scl; foundName = "user"; }
        else { printf("No LAN8680 (0x40) on PA%02d/PA%02d.\n", sda, scl); close_bus(handle, sda, scl); return 4; }
    } else {
        printf("\nProbing SERCOM I2C buses for the LAN8680 @ 0x40 ...\n");
        printf("(note: this briefly reconfigures display/SPI SERCOMs as I2C; pins are released after)\n");
        for (i = 0; i < (int)(sizeof(CAND)/sizeof(CAND[0])); ++i) {
            int s = CAND[i][0], c = CAND[i][1];
            printf("  %-4s PA%02d/PA%02d ... ", CAND_NAME[i], s, c); fflush(stdout);
            if (!open_bus(s, c, &handle)) { printf("OpenI2C failed\n"); continue; }
            if (is_lan8680(handle)) { printf("FOUND\n"); foundSda = s; foundScl = c; foundName = CAND_NAME[i]; break; }
            printf("no\n");
            close_bus(handle, s, c); handle = 0;
        }
        if (foundSda < 0) {
            printf("\nLAN8680 not found on any SERCOM I2C bus. It may be on a non-default\n"
                   "routing, or the housekeeping bus is not wired to a probeable SERCOM here.\n");
            return 4;
        }
    }

    /* identify */
    {
        uint16_t id1 = 0, id2 = 0, sts = 0;
        printf("\nLAN8680 found on %s (SDA=PA%02d SCL=PA%02d, addr 0x40)\n", foundName, foundSda, foundScl);
        if (rd16(handle, REG_PHY_ID1, &id1) && rd16(handle, REG_PHY_ID2, &id2)) {
            unsigned model = (id2 >> 4) & 0x3Fu, rev = id2 & 0x0Fu;
            printf("  PHY_ID1 (0x40) = 0x%04X\n", id1);
            printf("  PHY_ID2 (0x41) = 0x%04X   MODEL=0x%02X  REV=%u\n", id2, model, rev);
            printf("  -> %s\n", model == MODEL_LAN8680
                   ? "LAN8680 PMD Transceiver System Basis Chip confirmed." : "unexpected MODEL for a LAN8680.");
        } else printf("  ID read failed.\n");

        if (status) { if (rd16(handle, REG_STS0, &sts)) decode_sts0(sts); else printf("  STS0 read failed.\n"); }

        if (reg >= 0) {
            uint16_t v = 0;
            if (rd16(handle, (uint8_t)reg, &v)) printf("  reg 0x%02lX = 0x%04X\n", reg, v);
            else printf("  reg 0x%02lX read failed.\n", reg);
        }
    }

    close_bus(handle, foundSda, foundScl);
    return 0;
}
