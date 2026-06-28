/* sim_noise.h - PC-only seedable RNG + jitter/drift models (SIMULATION_SPEC.md §4.2).
 * Deterministic: same seed -> identical run (spec leitprinzip #3). NOT firmware code. */
#ifndef SIM_NOISE_H
#define SIM_NOISE_H

#include <stdint.h>

/* Sync offset-jitter models (spec §4.2). */
typedef enum {
    JITTER_GAUSS = 0,   /* N(0, sigma)                                        */
    JITTER_HEAVY_TAIL,  /* Gauss + occasional x10 outliers (PLCA/stack spikes)*/
    JITTER_LOAD_DEP,    /* sigma grows while "data load active" (⚠B2)         */
    JITTER_BIASED       /* constant positive per-node bias (⚠A3)              */
} jitter_model_t;

/* Oscillator thermal-drift variants (spec §4.1). */
typedef enum {
    DRIFT_NONE = 0,     /* variant a: 0 (reference)                           */
    DRIFT_SINE,         /* variant b: slow sine +-3 ppm, ~10 min period       */
    DRIFT_RAMP          /* variant c: +2 ppm ramp over the run                */
} drift_model_t;

/* Seedable PRNG state (xorshift128+ - small, fast, good enough, reproducible). */
typedef struct { uint64_t s0, s1; } rng_t;

void    rng_seed(rng_t *r, uint64_t seed);
double  rng_uniform(rng_t *r);          /* [0,1)                              */
double  rng_gauss(rng_t *r);            /* N(0,1)                             */
int64_t rng_thresh_e9(rng_t *r);        /* uniform in [0,1e9) for ⚠C4 dither  */

/* One sync-offset measurement-noise sample in ns, per the selected model.
 * load_active: 1 if the data stream is running (only used by JITTER_LOAD_DEP). */
double sim_jitter_ns(rng_t *r, jitter_model_t model, double sigma_ns,
                     double bias_ns, int load_active);

/* Thermal ppm offset at simulated time t_s for a node (added to ppm_base). */
double sim_drift_ppm(drift_model_t model, double t_s, double runtime_s);

#endif /* SIM_NOISE_H */
