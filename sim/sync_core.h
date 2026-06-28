/* sync_core.h - FIRMWARE-FAITHFUL sync + sample-clock core for the distributed
 * synchronous-ADC feasibility simulation.
 *
 * HARD MODULE BOUNDARY (SIMULATION_SPEC.md §3): everything here must compile
 * unchanged on the ATSAME54 - C99, no heap, no OS calls, fixed-width integers,
 * state passed in by struct (no globals). The PC-only simulation shell
 * (sim_main.c, sim_noise.c) may use anything.
 *
 * The constants and the PI update are NOT invented here - they are mirrored
 * verbatim from the firmware (see SIMULATION_SPEC.md §9.2: "firmware wins"):
 *
 *   Loop A (clock discipline)  : ntp_sync.c
 *       NTP_KI_DEN = 4         -> Ki = 1/4   (ntp_sync.c:64)
 *       Kp = 1 (full phase step)             (ntp_sync.c:364)
 *       ntp_rate_held / ntp_now_ns           (ntp_sync.c:68-114)
 *       OP_SET_OFFSET update order           (ntp_sync.c:344-367)
 *
 *   Loop B (sample clock)      : hwclk_cli.c cmd_pwm
 *       per_reg = 96e6/freq - 1 ; period length = per_reg+1                (hwclk_cli.c:765)
 *       8 kHz -> period length 12000 ticks (reg 11999); tick = 125/12 ns
 *
 * TWO DEVIATIONS from SYNC_ADC_KONZEPT.md (firmware/physics win, documented):
 *   1. s_rate_ppb is NEGATIVE for a fast (+ppm) oscillator. The concept's
 *      per_ideal = PER_NOM*(1 + s_rate_ppb/1e9) assumes the opposite sign.
 *      Firmware-faithful & physically correct: per_ideal = PER_NOM*(1 - s_rate_ppb/1e9).
 *   2. PER_NOM = 12000 is the period *length*; the HW register holds length-1.
 */
#ifndef SYNC_CORE_H
#define SYNC_CORE_H

#include <stdint.h>

/* ---- firmware constants (mirror ntp_sync.c / hwclk_cli.c; do not retune) ---- */
#define SC_KI_DEN     4            /* NTP_KI_DEN; Ki = 1/SC_KI_DEN (ntp_sync.c:64)  */
#define SC_KP_NUM     1            /* Kp = 1: full phase step (ntp_sync.c:364)      */
#define SC_HW_HZ      96000000LL   /* disciplined GEN5 clock (hwclk_cli.c)          */
#define SC_TICK_NUM   125LL        /* ns per tick = 125/12 (hwclk_cli.c hwclock_now_ns) */
#define SC_TICK_DEN   12LL
#define SC_PER_NOM    12000LL      /* sample period length in ticks @8 kHz = 96e6/8000  */
#define SC_ONE_E9     1000000000LL

/* Sample-clock dithering mode (SIMULATION_SPEC.md §4.4 + ⚠C4 variant). */
typedef enum {
    SC_DITHER_BRESENHAM = 0,  /* fixed sigma-delta accumulator (concept §5.3)      */
    SC_DITHER_NOISE     = 1   /* randomised threshold (⚠C4 anti-spurious variant)  */
} sc_dither_mode_t;

/* Per-node state. Mirrors the ntp_sync.c statics (Loop A) plus the Loop-B
 * sample-clock accumulator. Passed in by the caller - no globals (FW-portable). */
typedef struct {
    /* ---- Loop A: NTP PI discipline (ntp_sync.c) ---- */
    int64_t  s_offset_ns;     /* phase term P            (ntp_sync.c:55)            */
    int64_t  s_rate_ppb;      /* frequency integral I, ns/s; NEGATIVE for fast osc  */
    uint64_t s_lastSyncRaw;   /* raw ns at last SET_OFFSET (ntp_sync.c:60)          */
    int64_t  s_lastInterval;  /* raw ns between last two syncs (ntp_sync.c:61)      */
    int      s_synced;        /* (ntp_sync.c:58)                                    */
    uint32_t s_syncCount;     /* (ntp_sync.c:59)                                    */

    /* PROPOSED change (NOT in current firmware): phase proportional gain. kp=1.0
     * is the firmware behaviour (full phase snap, Kp=1). kp<1 low-passes the phase
     * correction -> trades convergence time for steady-state skew (see REPORT.md
     * "How long must convergence be"). The integral path (Ki=1/4) is unchanged. */
    double   kp;              /* default 1.0 = firmware                            */
    /* PROPOSED: integral smoothing. ki_den = NTP_KI_DEN (firmware 4). LARGER =
     * smaller Ki = quieter s_rate_ppb (less rate wander from offset noise) at the
     * cost of slower frequency lock and slower thermal-drift tracking (⚠C3). This
     * is the dominant lever at high sync jitter. */
    int      ki_den;         /* default 4 = firmware                             */
    int64_t  per_nom;        /* sample period length in ticks (default SC_PER_NOM=12000
                              * = 8 kHz; e.g. 24000 = 4 kHz). Set from config.    */

    /* ---- Loop B: sample-clock dithering (concept §5.3, sign-corrected) ---- */
    sc_dither_mode_t dither_mode;
    int64_t  per_resid;       /* Bresenham residual, scaled by 1e9 (fraction of a tick) */
    int64_t  noise_thresh;    /* current threshold for SC_DITHER_NOISE (scaled 1e9)  */
    uint64_t sample_k;        /* monotone sample index (coupled to physical samples) */
} sc_node_t;

/* Reset a node to power-on defaults. */
void sc_init(sc_node_t *n, sc_dither_mode_t dither_mode);

/* ---- Loop A read path (ntp_sync.c:68-114) ---- */
/* ns of frequency correction accrued since the last sync (= elapsed_s * rate_ppb). */
int64_t  sc_rate_held(const sc_node_t *n, uint64_t raw_ns);
/* disciplined NTP time = raw + phase + rate_held. */
int64_t  sc_now_ns(const sc_node_t *n, uint64_t raw_ns);

/* ---- Loop A discipline (ntp_sync.c:344-367 OP_SET_OFFSET) ---- */
/* adjust = the residual correction the master wants (= -measured offset).
 * raw_ns = the node's raw (undisciplined) counter in ns at this sync. */
void     sc_apply_offset(sc_node_t *n, int64_t adjust, uint64_t raw_ns);

/* ---- Loop B (concept §5.3, sign-corrected per header note #1) ---- */
/* Return the next sample period LENGTH in ticks (12000 or 12001), advance the
 * dither accumulator and the sample index. For SC_DITHER_NOISE pass a fresh
 * pseudo-random threshold in [0,1e9) via rnd_thresh_e9 (ignored for Bresenham). */
uint32_t sc_next_period(sc_node_t *n, int64_t rnd_thresh_e9);

/* tick<->ns helpers (exact integer, matches hwclock_now_ns 125/12). */
int64_t  sc_ticks_to_ns(int64_t ticks);
int64_t  sc_ns_to_ticks(int64_t ns);

#endif /* SYNC_CORE_H */
