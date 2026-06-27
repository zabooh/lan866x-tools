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
#include "config/default/system/time/sys_time.h"   /* SYS_TIME_* - reference clock for cmp */
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
#define HWCLK_GEN_TC2         5u      /* dedicated 96 MHz GCLK gen (DPLL1/2) -> TC2/TC3 */
#define HWCLK_TC2_HZ          96000000u  /* nominal DPLL1(192M)/2                    */

/* 64-bit extension of the free-running 32-bit TC2 (step 3): the high word is
 * incremented in the TC2 overflow ISR; the low 32 bits are the live TC2 COUNT. */
static volatile uint32_t s_tc2_hi    = 0;
static int               s_tc2_ready = 0;

static void gclk_sync(void)
{
    uint32_t g = 0u;   /* bounded: a generator sourced from a dead clock never syncs */
    while ((GCLK_REGS->GCLK_SYNCBUSY != 0u) && ++g < 1000000u) { }
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

/* Configure DPLL1 ~192 MHz from XOSC1 and wait for CLKRDY. Idempotent: if DPLL1 is
 * already enabled + ready it is left untouched (reconfiguring would glitch consumers
 * like the TC2 timebase). Rev A/D errata 2.13.1 (false unlock): LBYPASS + gate on
 * CLKRDY, not LOCK. Returns 1 if the DPLL1 clock is ready. */
static int ensure_dpll1(void)
{
    if ((OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLCTRLA & OSCCTRL_DPLLCTRLA_ENABLE_Msk)
     && (OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLSTATUS & OSCCTRL_DPLLSTATUS_CLKRDY_Msk))
        return 1;
    if (!ensure_xosc1()) return 0;

    OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLCTRLA = 0u;
    while (OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLSYNCBUSY & OSCCTRL_DPLLSYNCBUSY_ENABLE_Msk) { }
    /* Rev A/D errata 2.13.1: the FDPLL can false-unlock even with a stable output, which
     * GATES OFF the clock. The full workaround is BOTH LBYPASS=1 AND WUF=1 - that
     * decouples the output from the lock status so the clock keeps running through a
     * (transient) unlock. WUF was the missing piece that killed DPLL1 on on-the-fly
     * LDRFRAC changes. Gate on CLKRDY (not LOCK), then 10 ms settle (errata pseudo-code). */
    OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLCTRLB =
          OSCCTRL_DPLLCTRLB_REFCLK(OSCCTRL_DPLLCTRLB_REFCLK_XOSC1_Val)
        | OSCCTRL_DPLLCTRLB_DIV(HWCLK_DPLL1_DIV)
        | OSCCTRL_DPLLCTRLB_LTIME(0U)
        | OSCCTRL_DPLLCTRLB_FILTER(0U)
        | OSCCTRL_DPLLCTRLB_LBYPASS_Msk            /* errata 2.13.1 (rev A/D) */
        | OSCCTRL_DPLLCTRLB_WUF_Msk;               /* errata 2.13.1 (rev A/D) - keep clock on unlock */
    OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLRATIO =
          OSCCTRL_DPLLRATIO_LDR(HWCLK_DPLL1_LDR) | OSCCTRL_DPLLRATIO_LDRFRAC(0U);
    while (OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLSYNCBUSY & OSCCTRL_DPLLSYNCBUSY_DPLLRATIO_Msk) { }
    OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLCTRLA = OSCCTRL_DPLLCTRLA_ENABLE_Msk;
    while (OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLSYNCBUSY & OSCCTRL_DPLLSYNCBUSY_ENABLE_Msk) { }

    uint32_t t0 = plat_now_ms();
    while (plat_now_ms() - t0 < 200u)
        if (OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLSTATUS & OSCCTRL_DPLLSTATUS_CLKRDY_Msk) break;
    plat_sleep_ms(10);
    return (OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLSTATUS & OSCCTRL_DPLLSTATUS_CLKRDY_Msk) ? 1 : 0;
}

/* Read the current DPLL1 LDRFRAC from the RATIO register (read-only diagnostic).
 *
 * NOTE - no runtime LDRFRAC writes: on rev A/D an on-the-fly DPLLRATIO write
 * PERMANENTLY STALLS the DPLL1 output (verified on the board: TC2 freezes and does not
 * recover), even with the full errata workarounds applied - 2.13.1 WUF+LBYPASS (keep
 * the clock through a false-unlock) AND 2.13.2 (wait INTFLAG.DPLL1LDRTO for the ratio
 * update). The on-the-fly ratio-change feature is effectively unusable on this silicon.
 * Frequency discipline therefore stays in SOFTWARE (ntp_sync.c s_rate_ppb), which
 * already nulls the residual drift in the read path. */
static int hwclk_ldrfrac_get(void)
{
    return (int)((OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLRATIO & OSCCTRL_DPLLRATIO_LDRFRAC_Msk)
                 >> OSCCTRL_DPLLRATIO_LDRFRAC_Pos);
}

static void cmd_dpll(void)
{
    if (!ensure_xosc1()) {
        SYS_CONSOLE_PRINT("XOSC1 not ready -> run 'hwclk xosc' first (no 12 MHz source)\r\n");
        return;
    }
    int clkrdy = ensure_dpll1();

    uint32_t st = OSCCTRL_REGS->DPLL[1].OSCCTRL_DPLLSTATUS;
    int lock = (st & OSCCTRL_DPLLSTATUS_LOCK_Msk) ? 1 : 0;
    unsigned long fref = 12000000UL / (2u * (HWCLK_DPLL1_DIV + 1u));
    SYS_CONSOLE_PRINT("DPLL1 cfg  : REFCLK=XOSC1, DIV=%u (f_ref~%lu Hz), LDR=%u, LBYPASS+WUF (rev A/D)\r\n",
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

/* -------- step 3: hwclk now / hwclk wrap ----------------------------------- */
/* Bring up the free-running 64-bit TC2 timebase once: DPLL1 -> GCLK gen 5 (DPLL1/2
 * = 96 MHz) -> PCHCTRL[TC2_GCLK_ID] -> TC2 in 32-bit mode, OVF IRQ extends to 64-bit.
 * Idempotent. Returns 1 on success. TC0 (=SYS_TIME) is left alone. */
static int hwclk_tc2_init(void)
{
    if (s_tc2_ready) return 1;
    if (!ensure_dpll1()) return 0;

    /* dedicated 96 MHz generator from DPLL1/2, routed to the TC2/TC3 channel (26). */
    GCLK_REGS->GCLK_GENCTRL[HWCLK_GEN_TC2] =
        GCLK_GENCTRL_SRC(GCLK_GENCTRL_SRC_DPLL1_Val) | GCLK_GENCTRL_DIV(2U) | GCLK_GENCTRL_GENEN_Msk;
    gclk_sync();
    GCLK_REGS->GCLK_PCHCTRL[TC2_GCLK_ID] = GCLK_PCHCTRL_GEN(HWCLK_GEN_TC2) | GCLK_PCHCTRL_CHEN_Msk;
    while ((GCLK_REGS->GCLK_PCHCTRL[TC2_GCLK_ID] & GCLK_PCHCTRL_CHEN_Msk) == 0u) { }

    /* APB clocks: TC2 + its paired upper half TC3 (32-bit mode). */
    MCLK_REGS->MCLK_APBBMASK |= MCLK_APBBMASK_TC2_Msk | MCLK_APBBMASK_TC3_Msk;

    /* reset, then 32-bit free-running (normal-frequency: TOP = 0xFFFFFFFF), OVF IRQ. */
    TC2_REGS->COUNT32.TC_CTRLA = TC_CTRLA_SWRST_Msk;
    while (TC2_REGS->COUNT32.TC_SYNCBUSY & TC_SYNCBUSY_SWRST_Msk) { }
    TC2_REGS->COUNT32.TC_CTRLA = TC_CTRLA_MODE_COUNT32 | TC_CTRLA_PRESCALER_DIV1 | TC_CTRLA_PRESCSYNC_PRESC;
    TC2_REGS->COUNT32.TC_INTFLAG  = (uint8_t)TC_INTFLAG_Msk;     /* clear all */
    TC2_REGS->COUNT32.TC_INTENSET = (uint8_t)TC_INTENSET_OVF_Msk;
    s_tc2_hi = 0;

    NVIC_SetPriority(TC2_IRQn, 5);
    NVIC_EnableIRQ(TC2_IRQn);

    TC2_REGS->COUNT32.TC_CTRLA |= TC_CTRLA_ENABLE_Msk;
    while (TC2_REGS->COUNT32.TC_SYNCBUSY & TC_SYNCBUSY_ENABLE_Msk) { }

    s_tc2_ready = 1;
    return 1;
}

/* 1 once the XOSC1->DPLL1->TC2 hardware time base is running (ntp_sync.c read path). */
int hwclk_timebase_up(void) { return s_tc2_ready; }

/* Bring up the HW time base. MUST be called from the RUNNING phase (it busy-waits via
 * plat_sleep_ms(), which needs SYS_TIME/stack ticking) - never from APP_Initialize. */
void hwclk_timebase_start(void) { (void)hwclk_tc2_init(); }

/* TC2 overflow -> bump the 64-bit high word. Overrides the weak vector default. */
void __attribute__((used)) TC2_Handler(void)
{
    if (TC2_REGS->COUNT32.TC_INTFLAG & TC_INTFLAG_OVF_Msk) {
        TC2_REGS->COUNT32.TC_INTFLAG = (uint8_t)TC_INTFLAG_OVF_Msk;
        s_tc2_hi++;
    }
}

/* Glitch-free 64-bit read: re-read the high word around the COUNT read and retry if
 * an overflow slipped in between (hi1 != hi2). */
static uint64_t tc2_read64(void)
{
    uint32_t hi1, hi2, lo, g;
    do {
        hi1 = s_tc2_hi;
        TC2_REGS->COUNT32.TC_CTRLBSET = (uint8_t)TC_CTRLBSET_CMD_READSYNC;
        /* Bounded: if the TC2 GCLK is momentarily gone (DPLL1 disturbed) the READSYNC
         * never completes - never spin forever, it would freeze the whole superloop. */
        g = 0u; while ((TC2_REGS->COUNT32.TC_SYNCBUSY & TC_SYNCBUSY_CTRLB_Msk) && ++g < 200000u) { }
        g = 0u; while ((TC2_REGS->COUNT32.TC_CTRLBSET & TC_CTRLBSET_CMD_Msk)   && ++g < 200000u) { }
        lo  = TC2_REGS->COUNT32.TC_COUNT;
        hi2 = s_tc2_hi;
    } while (hi1 != hi2);
    return ((uint64_t)hi2 << 32) | lo;
}

static void cmd_now(void)
{
    if (!hwclk_tc2_init()) {
        SYS_CONSOLE_PRINT("DPLL1/TC2 not up -> run 'hwclk dpll' first\r\n");
        return;
    }
    /* measure the TC2 rate against SYS_TIME (plat_now_ms) over ~1 s. */
    uint64_t k0 = tc2_read64();
    uint32_t m0 = plat_now_ms();
    while (plat_now_ms() - m0 < 1000u) plat_sleep_ms(2);   /* pumps stack + console */
    uint64_t k1 = tc2_read64();
    uint32_t dms = plat_now_ms() - m0;

    uint64_t dticks = k1 - k0;
    uint64_t rate   = dms ? (dticks * 1000ULL / (uint64_t)dms) : 0u;   /* Hz, vs SYS_TIME */
    uint64_t ns     = k1 * 1000ULL / 96ULL;                            /* nominal 96 MHz */

    SYS_CONSOLE_PRINT("TC2 64-bit : %llu ticks  (hi=%lu lo=%lu)\r\n",
                      (unsigned long long)k1, (unsigned long)(k1 >> 32), (unsigned long)(uint32_t)k1);
    SYS_CONSOLE_PRINT("  time     : %llu.%09llu s (nominal 96 MHz)\r\n",
                      (unsigned long long)(ns / 1000000000ULL), (unsigned long long)(ns % 1000000000ULL));
    SYS_CONSOLE_PRINT("  rate     : %lu Hz over %lu ms (vs SYS_TIME/DFLL)  ->  %s\r\n",
                      (unsigned long)rate, (unsigned long)dms,
                      (rate > 94000000ULL && rate < 98000000ULL) ? "PASS (~96 MHz, monotonic)" : "FAIL");
}

static void cmd_wrap(void)
{
    if (!hwclk_tc2_init()) {
        SYS_CONSOLE_PRINT("DPLL1/TC2 not up -> run 'hwclk dpll' first\r\n");
        return;
    }
    /* load COUNT ~512 ticks (~5.3 us @96MHz) below 2^32 so OVF fires within ms. */
    uint32_t hi0 = s_tc2_hi;
    TC2_REGS->COUNT32.TC_COUNT = 0xFFFFFE00u;
    while (TC2_REGS->COUNT32.TC_SYNCBUSY & TC_SYNCBUSY_COUNT_Msk) { }
    plat_sleep_ms(5);
    uint32_t hi1 = s_tc2_hi;
    SYS_CONSOLE_PRINT("TC2 wrap   : high word %lu -> %lu  ->  %s\r\n",
                      (unsigned long)hi0, (unsigned long)hi1,
                      (hi1 != hi0) ? "PASS (OVF ISR extends to 64-bit)" : "FAIL (no overflow seen)");
}

/* -------- step 4: hwclk cmp ------------------------------------------------ */
/* HW clock time in ns from the free-running 64-bit TC2 at the nominal 96 MHz.
 * (Phase offset and fine-rate correction arrive in steps 5/6; this is raw HW time:
 * 1e9 / 96e6 = 125/12 ns per tick.) ntp_sync.c's ntp_raw_ns() reads this once up. */
uint64_t hwclock_now_ns(void)
{
    return tc2_read64() * 125ULL / 12ULL;
}

/* Compare the HW clock (XOSC1->DPLL1->TC2) against SYS_TIME (DFLL) over a window and
 * report the relative drift. Proves the read path works and that the HW clock is the
 * stable one (SYS_TIME/DFLL drifts ~+1900 ppm). */
static void cmd_cmp(uint32_t secs)
{
    if (!hwclk_tc2_init()) {
        SYS_CONSOLE_PRINT("TC2 not up -> run 'hwclk now' first\r\n");
        return;
    }
    if (secs == 0u) secs = 2u;
    uint32_t sysFreq = SYS_TIME_FrequencyGet();

    uint64_t hw0 = hwclock_now_ns();
    uint64_t sc0 = SYS_TIME_Counter64Get();
    uint32_t m0  = plat_now_ms();
    while (plat_now_ms() - m0 < secs * 1000u) plat_sleep_ms(2);   /* pumps stack + console */
    uint64_t hw1 = hwclock_now_ns();
    uint64_t sc1 = SYS_TIME_Counter64Get();

    uint64_t dhw  = hw1 - hw0;                                       /* real ns (accurate) */
    uint64_t dsys = (sc1 - sc0) * 1000000000ULL / (uint64_t)sysFreq; /* SYS_TIME ns        */
    int64_t  diff = (int64_t)dsys - (int64_t)dhw;                    /* + => SYS_TIME fast  */
    long     ppm  = dhw ? (long)(diff * 1000000LL / (int64_t)dhw) : 0;

    SYS_CONSOLE_PRINT("hwclk cmp  : window %lu s\r\n", (unsigned long)secs);
    SYS_CONSOLE_PRINT("  HW uptime: %llu.%09llu s (XOSC1->DPLL1->TC2 96 MHz)\r\n",
                      (unsigned long long)(hw1 / 1000000000ULL), (unsigned long long)(hw1 % 1000000000ULL));
    SYS_CONSOLE_PRINT("  elapsed  : HW %llu ns  vs  SYS_TIME %llu ns  (delta %+lld ns)\r\n",
                      (unsigned long long)dhw, (unsigned long long)dsys, (long long)diff);
    SYS_CONSOLE_PRINT("  drift    : SYS_TIME %+ld ppm vs HW  ->  %s\r\n", ppm,
                      (ppm > 500 && ppm < 4000)
                          ? "PASS (HW stable; DFLL drifts as expected)"
                          : ((ppm > -4000 && ppm < -500) ? "PASS (drift present; check sign)"
                                                         : "CHECK (drift outside expected DFLL band)"));
}

/* -------- step 6: frequency discipline (software) + read-only HW diagnostics --- */
/* hwclk ldrfrac : read-only - current DPLL1 LDRFRAC + measured DPLL1 freq. Runtime
 * LDRFRAC tuning is disabled on rev A/D (stalls the DPLL, see hwclk_ldrfrac_get). */
static void cmd_ldrfrac(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (!hwclk_tc2_init()) { SYS_CONSOLE_PRINT("DPLL1 not up -> run 'hwclk dpll' first\r\n"); return; }
    (void)enable_xosc32k();
    uint64_t hz = freqm_measure_src(GCLK_GENCTRL_SRC_DPLL1_Val, 4u, GCLK_GENCTRL_SRC_XOSC32K_Val);
    SYS_CONSOLE_PRINT("LDRFRAC    : %d / 31  (FIXED - on-the-fly tuning stalls DPLL on rev A/D)\r\n",
                      hwclk_ldrfrac_get());
    if (hz) {
        long ppm = (long)(((int64_t)hz - 192000000LL) * 1000000LL / 192000000LL);
        SYS_CONSOLE_PRINT("DPLL1 freq : %lu Hz  (%+ld ppm vs 192.000 MHz, XOSC32K ref)\r\n",
                          (unsigned long)hz, ppm);
    }
    SYS_CONSOLE_PRINT("discipline : frequency is corrected in SOFTWARE (ntp s_rate_ppb) - see 'ntp'\r\n");
}

/* hwclk hold : measured physical TC2 rate vs XOSC32K (= the residual the software loop
 * has to correct) and the implied free-running holdover. */
static void cmd_hold(void)
{
    if (!hwclk_tc2_init()) { SYS_CONSOLE_PRINT("DPLL1 not up -> run 'hwclk dpll' first\r\n"); return; }
    (void)enable_xosc32k();
    uint64_t hz = freqm_measure_src(GCLK_GENCTRL_SRC_DPLL1_Val, 4u, GCLK_GENCTRL_SRC_XOSC32K_Val);
    if (!hz) { SYS_CONSOLE_PRINT("hold: FREQM no XOSC32K reference\r\n"); return; }
    uint64_t tc = hz / 2u;
    long ppm = (long)(((int64_t)tc - 96000000LL) * 1000000LL / 96000000LL);
    SYS_CONSOLE_PRINT("physical TC2 : %lu Hz  (%+ld ppm vs 96.000 MHz, vs XOSC32K)\r\n",
                      (unsigned long)tc, ppm);
    SYS_CONSOLE_PRINT("  the software NTP loop corrects this residual in the read path;\r\n");
    SYS_CONSOLE_PRINT("  between-sync holdover ~= (locked osc.drift) x interval - see 'ntp' / 'ntp watch'.\r\n");
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
    if (argc >= 2 && strcmp(argv[1], "now") == 0)  { cmd_now();  return; }
    if (argc >= 2 && strcmp(argv[1], "wrap") == 0) { cmd_wrap(); return; }
    if (argc >= 2 && strcmp(argv[1], "cmp") == 0)  {
        cmd_cmp((argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 10) : 0u);
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "ldrfrac") == 0) { cmd_ldrfrac(argc - 2, argv + 2); return; }
    if (argc >= 2 && strcmp(argv[1], "hold") == 0)    { cmd_hold(); return; }
    SYS_CONSOLE_PRINT("usage: hwclk <rev|xosc [ulp]|dpll|now|wrap|cmp [secs]|ldrfrac|hold>\r\n");
    SYS_CONSOLE_PRINT("  rev        - silicon revision (DSU DID) + regulator + errata gate (step 0)\r\n");
    SYS_CONSOLE_PRINT("  xosc [ulp] - enable XOSC1 (12 MHz) and measure it with FREQM (step 1);\r\n");
    SYS_CONSOLE_PRINT("               'ulp' forces the internal OSCULP32K reference\r\n");
    SYS_CONSOLE_PRINT("  dpll       - bring up DPLL1 ~192 MHz from XOSC1 + FREQM verify (step 2)\r\n");
    SYS_CONSOLE_PRINT("  now        - 64-bit TC2 timebase (96 MHz): ticks, ns, measured rate (step 3)\r\n");
    SYS_CONSOLE_PRINT("  wrap       - force a TC2 overflow, verify the 64-bit high word ticks (step 3)\r\n");
    SYS_CONSOLE_PRINT("  cmp [secs] - compare HW clock vs SYS_TIME, report relative drift ppm (step 4)\r\n");
    SYS_CONSOLE_PRINT("  ldrfrac    - show DPLL1 LDRFRAC + measured freq (read-only; tuning is in SW) (step 6)\r\n");
    SYS_CONSOLE_PRINT("  hold       - physical TC2 rate vs XOSC32K + holdover note (step 6)\r\n");
}

static const SYS_CMD_DESCRIPTOR hwclk_cmd_tbl[] = {
    {"hwclk", (SYS_CMD_FNC) cmd_hwclk,
     ": HW time base: rev/xosc/dpll/now/wrap/cmp/ldrfrac/hold (steps 0-6)"},
};

void HWCLK_Init(void)
{
    SYS_CMD_ADDGRP(hwclk_cmd_tbl, sizeof(hwclk_cmd_tbl) / sizeof(*hwclk_cmd_tbl),
                   "hwclk", ": HW time base bring-up");
    /* NOTE: the HW time base is brought up later, on the first NTP_Task() call in the
     * RUNNING phase - NOT here. hwclk_tc2_init() busy-waits via plat_sleep_ms(), which
     * needs SYS_TIME/stack ticking; at APP_Initialize that hangs the boot. */
}
