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

/* Enable XOSC1 (12 MHz external MEMS at XIN1/PB22, XTALEN=0). Returns 1 if ready. */
static int ensure_xosc1(void)
{
    OSCCTRL_REGS->OSCCTRL_XOSCCTRL[1] = OSCCTRL_XOSCCTRL_ENABLE_Msk;
    uint32_t t0 = plat_now_ms();
    while ((OSCCTRL_REGS->OSCCTRL_STATUS & OSCCTRL_STATUS_XOSCRDY1_Msk) == 0u)
        if (plat_now_ms() - t0 > HWCLK_TMO_MS) return 0;
    return 1;
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

/* Set up the two scratch GCLK generators (MSR = msr_src/msr_div, REF = ref_src),
 * route them to the FREQM measure/reference channels, run ONE measurement and return
 * the *source* frequency in Hz (= measured GCLK output x msr_div, so a high clock can
 * be divided down to a FREQM-safe rate and recovered) - or 0 if the reference clock
 * is dead (FREQM BUSY never clears -> SW timeout, errata 2.28.1/2.28.2). Enabling the
 * REF generator also acts as a clock consumer, so an ONDEMAND oscillator (e.g.
 * XOSC32K) is requested here regardless of its RDY bit. */
static uint64_t freqm_measure_src(uint32_t msr_src, uint32_t msr_div, uint32_t ref_src)
{
    GCLK_REGS->GCLK_GENCTRL[HWCLK_GEN_MSR] =
        GCLK_GENCTRL_SRC(msr_src) | GCLK_GENCTRL_DIV(msr_div) | GCLK_GENCTRL_GENEN_Msk;
    GCLK_REGS->GCLK_GENCTRL[HWCLK_GEN_REF] =
        GCLK_GENCTRL_SRC(ref_src) | GCLK_GENCTRL_DIV(1U) | GCLK_GENCTRL_GENEN_Msk;
    gclk_sync();

    GCLK_REGS->GCLK_PCHCTRL[HWCLK_PCH_FREQM_MSR] = GCLK_PCHCTRL_GEN(HWCLK_GEN_MSR) | GCLK_PCHCTRL_CHEN_Msk;
    GCLK_REGS->GCLK_PCHCTRL[HWCLK_PCH_FREQM_REF] = GCLK_PCHCTRL_GEN(HWCLK_GEN_REF) | GCLK_PCHCTRL_CHEN_Msk;
    while ((GCLK_REGS->GCLK_PCHCTRL[HWCLK_PCH_FREQM_MSR] & GCLK_PCHCTRL_CHEN_Msk) == 0u) { }
    while ((GCLK_REGS->GCLK_PCHCTRL[HWCLK_PCH_FREQM_REF] & GCLK_PCHCTRL_CHEN_Msk) == 0u) { }

    MCLK_REGS->MCLK_APBAMASK |= MCLK_APBAMASK_FREQM_Msk;
    FREQM_REGS->FREQM_CTRLA = 0u;
    while (FREQM_REGS->FREQM_SYNCBUSY & FREQM_SYNCBUSY_ENABLE_Msk) { }
    FREQM_REGS->FREQM_CFGA  = (uint16_t)FREQM_CFGA_REFNUM(HWCLK_FREQM_REFNUM);
    FREQM_REGS->FREQM_CTRLA = FREQM_CTRLA_ENABLE_Msk;
    while (FREQM_REGS->FREQM_SYNCBUSY & FREQM_SYNCBUSY_ENABLE_Msk) { }

    FREQM_REGS->FREQM_STATUS = FREQM_STATUS_OVF_Msk;          /* clear sticky overflow */
    FREQM_REGS->FREQM_CTRLB  = FREQM_CTRLB_START_Msk;
    uint32_t t0 = plat_now_ms();
    while (FREQM_REGS->FREQM_STATUS & FREQM_STATUS_BUSY_Msk) {
        if (plat_now_ms() - t0 > HWCLK_TMO_MS) return 0u;    /* dead reference clock */
    }
    uint32_t val = FREQM_REGS->FREQM_VALUE & FREQM_VALUE_VALUE_Msk;
    /* f_msr_pin = VALUE * f_ref / REFNUM (f_ref ~32768); source = f_msr_pin * msr_div. */
    return (uint64_t)val * 32768ULL * (uint64_t)msr_div / (uint64_t)HWCLK_FREQM_REFNUM;
}

/* Enable XOSC32K = external 32.768 kHz MEMS (Y400) at XIN32 (XTALEN=0). Polls RDY
 * up to ~1 s and always prints the raw control/status so we can see the truth even
 * if RDY misbehaves in external-clock mode. Returns 1 if RDY asserted. */
static int enable_xosc32k(void)
{
    OSC32KCTRL_REGS->OSC32KCTRL_XOSC32K =
        (uint16_t)(OSC32KCTRL_XOSC32K_ENABLE_Msk
                 | OSC32KCTRL_XOSC32K_EN32K_Msk
                 | OSC32KCTRL_XOSC32K_CGM_XT          /* defined gain value (XTALEN=0 bypasses amp) */
                 | OSC32KCTRL_XOSC32K_STARTUP(0U));   /* XTALEN=0 -> external clock at XIN32 */
    uint32_t t0 = plat_now_ms();
    int rdy = 0;
    while (plat_now_ms() - t0 < 1000u) {
        if (OSC32KCTRL_REGS->OSC32KCTRL_STATUS & OSC32KCTRL_STATUS_XOSC32KRDY_Msk) { rdy = 1; break; }
    }
    SYS_CONSOLE_PRINT("XOSC32K    : ctrl=0x%04X status=0x%08lX RDY=%d\r\n",
                      (unsigned)(OSC32KCTRL_REGS->OSC32KCTRL_XOSC32K & 0xFFFFu),
                      (unsigned long)OSC32KCTRL_REGS->OSC32KCTRL_STATUS, rdy);
    return rdy;
}

static void cmd_xosc(int forceUlp)
{
    /* 1) enable XOSC1 = external 12 MHz MEMS at XIN1/PB22 (XTALEN=0, no IMULT/IPTAT). */
    int rdy = ensure_xosc1();
    SYS_CONSOLE_PRINT("XOSC1 enable (XTALEN=0, XIN1/PB22): RDY=%d\r\n", rdy);
    if (!rdy) {
        SYS_CONSOLE_PRINT("  ** XOSCRDY1 never set -> no 12 MHz clock at XIN1 (check Y401 / R471 0R) **\r\n");
        return;
    }

    /* 2) measure XOSC1 with the accurate external XOSC32K reference; fall back to the
     *    internal OSCULP32K if XOSC32K's clock is absent (FREQM times out). */
    uint64_t hz; const char *refname; int accurate;
    if (forceUlp) {
        hz = freqm_measure_src(GCLK_GENCTRL_SRC_XOSC1_Val, 1u, GCLK_GENCTRL_SRC_OSCULP32K_Val);
        refname = "OSCULP32K (forced, approx)"; accurate = 0;
    } else {
        (void)enable_xosc32k();                                   /* prints raw regs + RDY */
        hz = freqm_measure_src(GCLK_GENCTRL_SRC_XOSC1_Val, 1u, GCLK_GENCTRL_SRC_XOSC32K_Val);
        if (hz != 0u) { refname = "XOSC32K (external MEMS)"; accurate = 1; }
        else {
            hz = freqm_measure_src(GCLK_GENCTRL_SRC_XOSC1_Val, 1u, GCLK_GENCTRL_SRC_OSCULP32K_Val);
            refname = "OSCULP32K (internal, approx - XOSC32K unavailable)"; accurate = 0;
        }
    }

    if (hz == 0u) {
        SYS_CONSOLE_PRINT("FREQM: no usable reference clock -> measurement failed\r\n");
        return;
    }

    long dev_ppm = (long)(((int64_t)hz - 12000000LL) * 1000000LL / 12000000LL);
    SYS_CONSOLE_PRINT("FREQM ref  : %s, REFNUM=%u\r\n", refname, (unsigned)HWCLK_FREQM_REFNUM);
    SYS_CONSOLE_PRINT("XOSC1 freq : %lu Hz  (~%lu.%03lu MHz)\r\n",
                      (unsigned long)hz, (unsigned long)(hz / 1000000ULL),
                      (unsigned long)((hz % 1000000ULL) / 1000ULL));
    if (accurate) {
        SYS_CONSOLE_PRINT("  deviation : %+ld ppm from 12.000 MHz  ->  %s\r\n",
                          dev_ppm,
                          (hz > 11976000ULL && hz < 12024000ULL) ? "PASS (~12 MHz, +/-2000 ppm)" : "FAIL");
    } else {
        SYS_CONSOLE_PRINT("  note     : internal OSCULP32K ref (~+/-several %%) -> presence check only\r\n");
        SYS_CONSOLE_PRINT("  result   : %s\r\n",
                          (hz > 11000000ULL && hz < 13000000ULL) ? "PASS (~12 MHz present)" : "FAIL");
    }
}

/* -------- step 2: hwclk dpll ----------------------------------------------- */
/* DPLL1 = XOSC1 / (2*(DIV+1)) ~ 32.79 kHz reference, x (LDR+1) ~ 192 MHz.
 * 12e6 / (2*183) = 32786.9 Hz ; * 5856 = 192.00 MHz. The low reference gives the
 * fine LDRFRAC step (~5 ppm/LSB) used later for frequency discipline. */
#define HWCLK_DPLL1_DIV   182u
#define HWCLK_DPLL1_LDR   5855u

static void cmd_dpll(void)
{
    if (!ensure_xosc1()) {
        SYS_CONSOLE_PRINT("XOSC1 not ready -> run 'hwclk xosc' first (no 12 MHz source)\r\n");
        return;
    }

    /* Reconfigure DPLL1 from scratch (disable first). Rev A/D errata 2.13.1
     * (false unlock): set LBYPASS and gate on CLKRDY, not LOCK. */
    OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLCTRLA = 0u;
    while (OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLSYNCBUSY & OSCCTRL_DPLLSYNCBUSY_ENABLE_Msk) { }

    OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLCTRLB =
          OSCCTRL_DPLLCTRLB_REFCLK(OSCCTRL_DPLLCTRLB_REFCLK_XOSC1_Val)
        | OSCCTRL_DPLLCTRLB_DIV(HWCLK_DPLL1_DIV)
        | OSCCTRL_DPLLCTRLB_LTIME(0U)
        | OSCCTRL_DPLLCTRLB_FILTER(0U)
        | OSCCTRL_DPLLCTRLB_LBYPASS_Msk;          /* errata 2.13.1 (rev A/D) */
    OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLRATIO =
          OSCCTRL_DPLLRATIO_LDR(HWCLK_DPLL1_LDR) | OSCCTRL_DPLLRATIO_LDRFRAC(0U);
    while (OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLSYNCBUSY & OSCCTRL_DPLLSYNCBUSY_DPLLRATIO_Msk) { }

    OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLCTRLA = OSCCTRL_DPLLCTRLA_ENABLE_Msk;
    while (OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLSYNCBUSY & OSCCTRL_DPLLSYNCBUSY_ENABLE_Msk) { }

    /* Gate on CLKRDY (LBYPASS: LOCK may falsely toggle on rev A/D), then let settle. */
    uint32_t t0 = plat_now_ms(); int clkrdy = 0;
    while (plat_now_ms() - t0 < 200u) {
        if (OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLSTATUS & OSCCTRL_DPLLSTATUS_CLKRDY_Msk) { clkrdy = 1; break; }
    }
    plat_sleep_ms(5);

    uint32_t st = OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLSTATUS;
    int lock = (st & OSCCTRL_DPLLSTATUS_LOCK_Msk) ? 1 : 0;
    unsigned long fref = 12000000UL / (2u * (HWCLK_DPLL1_DIV + 1u));
    SYS_CONSOLE_PRINT("DPLL1 cfg  : REFCLK=XOSC1, DIV=%u (f_ref~%lu Hz), LDR=%u, LBYPASS (rev A/D)\r\n",
                      (unsigned)HWCLK_DPLL1_DIV, fref, (unsigned)HWCLK_DPLL1_LDR);
    SYS_CONSOLE_PRINT("DPLL1 lock : CLKRDY=%d LOCK=%d (status=0x%08lX)\r\n",
                      clkrdy, lock, (unsigned long)st);
    if (!clkrdy) {
        SYS_CONSOLE_PRINT("  ** CLKRDY never set -> DPLL1 not running (ref out of 32k..3.2MHz? XOSC1 down?) **\r\n");
        return;
    }

    /* Measure the DPLL1 output: route DPLL1/4 (~48 MHz) to FREQM, XOSC32K reference. */
    (void)enable_xosc32k();
    int accurate = 1;
    uint64_t hz = freqm_measure_src(GCLK_GENCTRL_SRC_DPLL1_Val, 4u, GCLK_GENCTRL_SRC_XOSC32K_Val);
    if (hz == 0u) {
        hz = freqm_measure_src(GCLK_GENCTRL_SRC_DPLL1_Val, 4u, GCLK_GENCTRL_SRC_OSCULP32K_Val);
        accurate = 0;
    }
    if (hz == 0u) { SYS_CONSOLE_PRINT("  FREQM: no usable reference -> cannot measure DPLL1\r\n"); return; }

    long dev_ppm = (long)(((int64_t)hz - 192000000LL) * 1000000LL / 192000000LL);
    SYS_CONSOLE_PRINT("DPLL1 freq : %lu Hz  (~%lu.%03lu MHz)%s\r\n",
                      (unsigned long)hz, (unsigned long)(hz / 1000000ULL),
                      (unsigned long)((hz % 1000000ULL) / 1000ULL),
                      accurate ? "" : "  [OSCULP32K ref, approx]");
    if (accurate) {
        SYS_CONSOLE_PRINT("  target 192.000 MHz, dev %+ld ppm  ->  %s\r\n", dev_ppm,
                          (hz > 191000000ULL && hz < 193000000ULL) ? "PASS (CLKRDY + ~192 MHz)" : "FAIL");
    } else {
        SYS_CONSOLE_PRINT("  result   : %s\r\n",
                          (hz > 188000000ULL && hz < 196000000ULL) ? "PASS (~192 MHz present)" : "FAIL");
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
    if (argc >= 2 && strcmp(argv[1], "dpll") == 0) { cmd_dpll(); return; }
    SYS_CONSOLE_PRINT("usage: hwclk <rev|xosc [ulp]|dpll>\r\n");
    SYS_CONSOLE_PRINT("  rev        - silicon revision (DSU DID) + regulator + errata gate (step 0)\r\n");
    SYS_CONSOLE_PRINT("  xosc [ulp] - enable XOSC1 (12 MHz) and measure it with FREQM (step 1);\r\n");
    SYS_CONSOLE_PRINT("               'ulp' forces the internal OSCULP32K reference\r\n");
    SYS_CONSOLE_PRINT("  dpll       - bring up DPLL1 ~192 MHz from XOSC1 + FREQM verify (step 2)\r\n");
}

static const SYS_CMD_DESCRIPTOR hwclk_cmd_tbl[] = {
    {"hwclk", (SYS_CMD_FNC) cmd_hwclk,
     ": HW time-base bring-up: rev (s0), xosc [ulp] (s1), dpll (s2)"},
};

void HWCLK_Init(void)
{
    SYS_CMD_ADDGRP(hwclk_cmd_tbl, sizeof(hwclk_cmd_tbl) / sizeof(*hwclk_cmd_tbl),
                   "hwclk", ": HW time base bring-up");
}
