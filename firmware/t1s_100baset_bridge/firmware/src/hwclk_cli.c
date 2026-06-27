/*
 * hwclk_cli.c - "hwclk" CLI group: incremental bring-up of the disciplined
 * hardware time base (XOSC1 -> DPLL1 -> TC -> EVSYS), step by step, each step
 * verified on the MCU before the next. See:
 *   docs/HW_TIMEBASE_BRINGUP_STEPS.md      (the step list + pass criteria)
 *   docs/HW_TIMEBASE_B_C_IMPLEMENTATION.md  (register sequences)
 *   docs/HW_TIMEBASE_OPTIONS.md             (why XOSC1 + DPLL1)
 *
 * Implemented so far:
 *   step 0  hwclk rev        - DSU DID silicon revision + SUPC regulator (errata gate)
 *   step 1  hwclk xosc [ulp] - enable XOSC1 (12 MHz ext MEMS, XTALEN=0) + FREQM measure
 *
 * Non-invasive: 'rev' is read-only; 'xosc' enables XOSC1, XOSC32K and a couple of
 * scratch GCLK generators + FREQM, but does NOT touch DPLL0 / GCLK0/1/2 / the CPU
 * clock, so the running bridge is unaffected. Single-thread, no C++.
 *
 * Schematic fact (SAM E54 Curiosity Ultra R3): the 12 MHz source is an active MEMS
 * oscillator DSC6003C12A on XIN1 / PB22 / pin 97 = XOSC1 -> external-clock mode,
 * XTALEN=0 (not a crystal, not XOSC0). The 32.768 kHz source is likewise a MEMS
 * oscillator (DSC6083CE2A) on XOSC32K.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "definitions.h"
#include "config/default/system/console/sys_console.h"
#include "system/command/sys_command.h"
#include "plat.h"            /* plat_now_ms() - software timeouts (errata 2.28.x) */
#include "lan866x_cli.h"

/* FREQM generic-clock peripheral-channel indices (DFP: FREQM_GCLK_ID_MSR/REF). */
#define HWCLK_PCH_FREQM_MSR   5u
#define HWCLK_PCH_FREQM_REF   6u
/* Scratch GCLK generators for the FREQM measurement. GEN0/1/2 are in use
 * (120/60/1 MHz); 3..11 are free. Keep clear of GEN5 (planned 96 MHz DPLL1). */
#define HWCLK_GEN_MSR         6u      /* XOSC1 (12 MHz) -> FREQM measure clock   */
#define HWCLK_GEN_REF         7u      /* 32 kHz         -> FREQM reference clock */
#define HWCLK_FREQM_REFNUM    255u    /* 255/32768 ~ 7.78 ms window               */
#define HWCLK_TMO_MS          50u

static void gclk_sync(void)
{
    while (GCLK_REGS->GCLK_SYNCBUSY != 0u) { /* wait for GENCTRL writes */ }
}

/* -------- step 0: hwclk rev ------------------------------------------------ */
static void cmd_rev(void)
{
    uint32_t did = DSU_REGS->DSU_DID;
    unsigned rev = (unsigned)((did & DSU_DID_REVISION_Msk) >> DSU_DID_REVISION_Pos);
    unsigned die = (unsigned)((did & DSU_DID_DIE_Msk)      >> DSU_DID_DIE_Pos);
    unsigned ser = (unsigned)((did & DSU_DID_SERIES_Msk)   >> DSU_DID_SERIES_Pos);
    int buck     = (SUPC_REGS->SUPC_VREG & SUPC_VREG_SEL_Msk) ? 1 : 0;

    SYS_CONSOLE_PRINT("DSU DID    : 0x%08lX\r\n", (unsigned long)did);
    SYS_CONSOLE_PRINT("  revision : %c   (REVISION field = %u)\r\n", (char)('A' + rev), rev);
    SYS_CONSOLE_PRINT("  series   : %u    die: %u\r\n", ser, die);
    SYS_CONSOLE_PRINT("Regulator  : %s\r\n", buck ? "BUCK" : "LDO");

    SYS_CONSOLE_PRINT("Errata gate (DS80000748):\r\n");
    SYS_CONSOLE_PRINT("  2.19.1 FDPLL needs LDO : %s\r\n",
                      buck ? "** BUCK active -> switch to LDO before DPLL1 **" : "OK (LDO)");
    if (rev <= 3u)   /* A=0 .. D=3 */
        SYS_CONSOLE_PRINT("  2.13.1 FDPLL false-unlock: rev %c is early (A-D) -> use LBYPASS/WUF/CLKRDY at DPLL1 bring-up\r\n",
                          (char)('A' + rev));
    else
        SYS_CONSOLE_PRINT("  2.13.1 FDPLL false-unlock: not applicable (rev %c, fixed in F/G)\r\n",
                          (char)('A' + rev));
}

/* -------- step 1: hwclk xosc ----------------------------------------------- */

/* Bring up a 32 kHz FREQM reference. Prefer the accurate external XOSC32K MEMS;
 * fall back to internal OSCULP32K (rough) if it does not come ready. Returns the
 * GCLK GENCTRL SRC value; *accurate is set when XOSC32K was used. */
static uint32_t enable_ref32k(const char **name, int *accurate)
{
    OSC32KCTRL_REGS->OSC32KCTRL_XOSC32K =
        (uint16_t)(OSC32KCTRL_XOSC32K_ENABLE_Msk
                 | OSC32KCTRL_XOSC32K_EN32K_Msk
                 | OSC32KCTRL_XOSC32K_STARTUP(0U));   /* XTALEN=0 -> external 32 kHz clock */
    uint32_t t0 = plat_now_ms();
    while ((OSC32KCTRL_REGS->OSC32KCTRL_STATUS & OSC32KCTRL_STATUS_XOSC32KRDY_Msk) == 0u) {
        if (plat_now_ms() - t0 > HWCLK_TMO_MS) {
            *name = "OSCULP32K (internal, approx)"; *accurate = 0;
            return GCLK_GENCTRL_SRC_OSCULP32K_Val;
        }
    }
    *name = "XOSC32K (external MEMS)"; *accurate = 1;
    return GCLK_GENCTRL_SRC_XOSC32K_Val;
}

static void cmd_xosc(int forceUlp)
{
    /* 1) enable XOSC1 = external 12 MHz MEMS at XIN1/PB22 (XTALEN=0, no IMULT/IPTAT). */
    OSCCTRL_REGS->OSCCTRL_XOSCCTRL[1] = OSCCTRL_XOSCCTRL_ENABLE_Msk;
    uint32_t t0 = plat_now_ms();
    int rdy = 1;
    while ((OSCCTRL_REGS->OSCCTRL_STATUS & OSCCTRL_STATUS_XOSCRDY1_Msk) == 0u) {
        if (plat_now_ms() - t0 > HWCLK_TMO_MS) { rdy = 0; break; }
    }
    SYS_CONSOLE_PRINT("XOSC1 enable (XTALEN=0, XIN1/PB22): RDY=%d\r\n", rdy);
    if (!rdy) {
        SYS_CONSOLE_PRINT("  ** XOSCRDY1 never set -> no 12 MHz clock at XIN1 (check Y401 / R471 0R) **\r\n");
        return;
    }

    /* 2) 32 kHz FREQM reference. */
    const char *refname; int refAccurate;
    uint32_t refsrc;
    if (forceUlp) { refsrc = GCLK_GENCTRL_SRC_OSCULP32K_Val; refname = "OSCULP32K (forced, approx)"; refAccurate = 0; }
    else          { refsrc = enable_ref32k(&refname, &refAccurate); }

    /* 3) scratch GCLK generators: MSR = XOSC1 (12 MHz, /1), REF = 32 kHz (/1). */
    GCLK_REGS->GCLK_GENCTRL[HWCLK_GEN_MSR] =
        GCLK_GENCTRL_SRC(GCLK_GENCTRL_SRC_XOSC1_Val) | GCLK_GENCTRL_DIV(1U) | GCLK_GENCTRL_GENEN_Msk;
    GCLK_REGS->GCLK_GENCTRL[HWCLK_GEN_REF] =
        GCLK_GENCTRL_SRC(refsrc) | GCLK_GENCTRL_DIV(1U) | GCLK_GENCTRL_GENEN_Msk;
    gclk_sync();

    /* 4) route the two generators to the FREQM measure/reference channels. */
    GCLK_REGS->GCLK_PCHCTRL[HWCLK_PCH_FREQM_MSR] = GCLK_PCHCTRL_GEN(HWCLK_GEN_MSR) | GCLK_PCHCTRL_CHEN_Msk;
    GCLK_REGS->GCLK_PCHCTRL[HWCLK_PCH_FREQM_REF] = GCLK_PCHCTRL_GEN(HWCLK_GEN_REF) | GCLK_PCHCTRL_CHEN_Msk;
    while ((GCLK_REGS->GCLK_PCHCTRL[HWCLK_PCH_FREQM_MSR] & GCLK_PCHCTRL_CHEN_Msk) == 0u) { }
    while ((GCLK_REGS->GCLK_PCHCTRL[HWCLK_PCH_FREQM_REF] & GCLK_PCHCTRL_CHEN_Msk) == 0u) { }

    /* 5) FREQM: APB clock, REFNUM (set while disabled), then enable. */
    MCLK_REGS->MCLK_APBAMASK |= MCLK_APBAMASK_FREQM_Msk;
    FREQM_REGS->FREQM_CTRLA = 0u;
    while (FREQM_REGS->FREQM_SYNCBUSY & FREQM_SYNCBUSY_ENABLE_Msk) { }
    FREQM_REGS->FREQM_CFGA  = (uint16_t)FREQM_CFGA_REFNUM(HWCLK_FREQM_REFNUM);
    FREQM_REGS->FREQM_CTRLA = FREQM_CTRLA_ENABLE_Msk;
    while (FREQM_REGS->FREQM_SYNCBUSY & FREQM_SYNCBUSY_ENABLE_Msk) { }

    /* 6) one measurement; poll BUSY with a software timeout (errata 2.28.1/2.28.2). */
    FREQM_REGS->FREQM_STATUS = FREQM_STATUS_OVF_Msk;          /* clear sticky overflow */
    FREQM_REGS->FREQM_CTRLB  = FREQM_CTRLB_START_Msk;
    t0 = plat_now_ms();
    int tmo = 0;
    while (FREQM_REGS->FREQM_STATUS & FREQM_STATUS_BUSY_Msk) {
        if (plat_now_ms() - t0 > HWCLK_TMO_MS) { tmo = 1; break; }
    }
    if (tmo) {
        SYS_CONSOLE_PRINT("FREQM: measurement timed out (no reference clock? ref=%s)\r\n", refname);
        return;
    }

    uint32_t val = FREQM_REGS->FREQM_VALUE & FREQM_VALUE_VALUE_Msk;
    int ovf      = (FREQM_REGS->FREQM_STATUS & FREQM_STATUS_OVF_Msk) ? 1 : 0;

    /* f_msr = VALUE * f_ref / REFNUM ; f_ref nominal 32768 Hz. */
    uint64_t hz = (uint64_t)val * 32768ULL / (uint64_t)HWCLK_FREQM_REFNUM;
    long dev_ppm = (long)(((int64_t)hz - 12000000LL) * 1000000LL / 12000000LL);

    SYS_CONSOLE_PRINT("FREQM ref  : %s, REFNUM=%u, VALUE=%lu%s\r\n",
                      refname, (unsigned)HWCLK_FREQM_REFNUM, (unsigned long)val, ovf ? "  (OVF!)" : "");
    SYS_CONSOLE_PRINT("XOSC1 freq : %lu Hz  (~%lu.%03lu MHz)\r\n",
                      (unsigned long)hz, (unsigned long)(hz / 1000000ULL),
                      (unsigned long)((hz % 1000000ULL) / 1000ULL));

    if (refAccurate) {
        SYS_CONSOLE_PRINT("  deviation : %+ld ppm from 12.000 MHz  ->  %s\r\n",
                          dev_ppm,
                          (hz > 11976000ULL && hz < 12024000ULL) ? "PASS (~12 MHz, +/-2000 ppm)" : "FAIL");
    } else {
        /* OSCULP32K is only ~+/-few % accurate: confirm PRESENCE, ppm not trustworthy. */
        SYS_CONSOLE_PRINT("  note     : ref is internal OSCULP32K (~+/-several %%) -> presence check only\r\n");
        SYS_CONSOLE_PRINT("  result   : %s   (run without 'ulp' for an accurate XOSC32K-referenced ppm)\r\n",
                          (hz > 11000000ULL && hz < 13000000ULL) ? "PASS (~12 MHz present)" : "FAIL");
    }
}

/* -------- dispatch + registration ----------------------------------------- */
static void cmd_hwclk(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    (void)pCmdIO;
    if (argc >= 2 && strcmp(argv[1], "rev") == 0)  { cmd_rev(); return; }
    if (argc >= 2 && strcmp(argv[1], "xosc") == 0) {
        cmd_xosc(argc >= 3 && strcmp(argv[2], "ulp") == 0);
        return;
    }
    SYS_CONSOLE_PRINT("usage: hwclk <rev|xosc [ulp]>\r\n");
    SYS_CONSOLE_PRINT("  rev        - silicon revision (DSU DID) + regulator + errata gate (step 0)\r\n");
    SYS_CONSOLE_PRINT("  xosc [ulp] - enable XOSC1 (12 MHz) and measure it with FREQM (step 1);\r\n");
    SYS_CONSOLE_PRINT("               'ulp' forces the internal OSCULP32K reference\r\n");
}

static const SYS_CMD_DESCRIPTOR hwclk_cmd_tbl[] = {
    {"hwclk", (SYS_CMD_FNC) cmd_hwclk,
     ": HW time-base bring-up: 'hwclk rev' (step 0), 'hwclk xosc [ulp]' (step 1)"},
};

void HWCLK_Init(void)
{
    SYS_CMD_ADDGRP(hwclk_cmd_tbl, sizeof(hwclk_cmd_tbl) / sizeof(*hwclk_cmd_tbl),
                   "hwclk", ": HW time base bring-up");
}
