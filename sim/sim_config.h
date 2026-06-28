/* sim_config.h - simulation parameters (SIMULATION_SPEC.md §3, §9.5).
 * Edit + rebuild, or override per run via CLI flags (see sim_main.c --help).
 * Every run logs its seed + model into run_summary.csv for reproducibility. */
#ifndef SIM_CONFIG_H
#define SIM_CONFIG_H

#include "sim_noise.h"
#include "sync_core.h"

/* Fault injection for M6 (⚠D1/⚠D4). One fault on one node at one time. */
typedef enum {
    FAULT_NONE = 0,
    FAULT_GO_LOSS,      /* node misses GO -> anchors index 0 at the wrong instant   */
    FAULT_REBOOT,       /* node resets, rejoins with a wrong index (pre-lock, ⚠D4)  */
    FAULT_SAMPLE_LOSS   /* DMA overflow: physical samples lost, index not advanced  */
} fault_kind_t;

typedef struct {
    /* topology / runtime */
    int      n_nodes;          /* number of follower nodes (target 7)             */
    double   runtime_s;        /* simulated seconds                               */
    uint64_t seed;             /* master RNG seed (deterministic)                 */

    /* timebase */
    int64_t  sync_interval_ticks;  /* 125 ms @ 96 MHz = 12,000,000 ticks          */
    int64_t  startup_lead_ticks;   /* GO lead-in before sampling (spec §4.5)       */
    double   go_at_s;              /* when the master broadcasts GO (after lock)   */

    /* oscillator (spec §4.1) */
    double   ppm_base_lo;      /* per-node static ppm offset, uniform [lo,hi]     */
    double   ppm_base_hi;
    drift_model_t drift_model; /* thermal drift variant a/b/c                     */

    /* sync jitter (spec §4.2) */
    jitter_model_t jitter_model;
    double   sigma_ns;         /* base offset-jitter sigma                        */
    double   bias_ns;          /* per-node bias amplitude (JITTER_BIASED)         */
    int      rounds_R;         /* robust estimator: rounds per node per sync      */

    /* sample clock (spec §4.4) */
    sc_dither_mode_t dither_mode;

    /* logging */
    double   ts_sample_ms;     /* timeseries row period (ms) - don't log every tick */
    const char *out_dir;       /* where CSVs are written                          */
    int      append;           /* append a row to run_summary.csv (sweeps)        */
    int      detail_csv;       /* write per-run timeseries/pairwise/dither/fault   */

    /* fault injection (M6) */
    fault_kind_t fault;        /* FAULT_NONE by default                           */
    int      fault_node;       /* which node (default 3); -1 = none               */
    double   fault_time_s;     /* when the fault hits                              */
    int      fault_samples;    /* J lost samples (FAULT_SAMPLE_LOSS)              */
    double   fault_go_off_us;  /* mis-anchor offset (FAULT_GO_LOSS)               */
    int      hb_tol_samples;   /* consistency-check tolerance                      */
} sim_config_t;

/* Built-in defaults. Pessimistic-but-plausible per spec leitprinzip #4. */
static inline sim_config_t sim_config_default(void)
{
    sim_config_t c;
    c.n_nodes             = 7;
    c.runtime_s           = 60.0;
    c.seed                = 1u;

    c.sync_interval_ticks = SC_HW_HZ / 8;        /* 125 ms                        */
    c.startup_lead_ticks  = SC_HW_HZ / 2;        /* 500 ms GO lead (spec §5.1)    */
    c.go_at_s             = 2.0;                 /* broadcast GO after clocks lock */

    c.ppm_base_lo         = 20.0;                /* real ~+28 ppm rest, spread    */
    c.ppm_base_hi         = 35.0;
    c.drift_model         = DRIFT_NONE;

    c.jitter_model        = JITTER_GAUSS;
    c.sigma_ns            = 2000.0;              /* 2 us (spec §4.2 example)       */
    c.bias_ns             = 1000.0;
    c.rounds_R            = 8;                   /* like the PC tool min-delay set */

    c.dither_mode         = SC_DITHER_BRESENHAM;

    c.ts_sample_ms        = 125.0;              /* one row per sync by default     */
    c.out_dir             = ".";
    c.append              = 0;
    c.detail_csv          = 1;

    c.fault               = FAULT_NONE;
    c.fault_node          = 3;
    c.fault_time_s        = 10.0;
    c.fault_samples       = 40;     /* lose 40 samples = 5 ms of stream            */
    c.fault_go_off_us     = 1000.0; /* mis-anchor by 1 ms = 8 samples              */
    c.hb_tol_samples      = 2;      /* flag if |reported - expected| > 2 samples   */
    return c;
}

#endif /* SIM_CONFIG_H */
