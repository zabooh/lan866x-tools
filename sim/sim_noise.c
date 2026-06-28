/* sim_noise.c - see sim_noise.h. PC-only. */
#include "sim_noise.h"
#include <math.h>

/* ---- xorshift128+ : deterministic, seed-reproducible ---- */
static uint64_t splitmix64(uint64_t *x)
{
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void rng_seed(rng_t *r, uint64_t seed)
{
    uint64_t sm = seed ? seed : 0xDEADBEEFCAFEF00DULL;
    r->s0 = splitmix64(&sm);
    r->s1 = splitmix64(&sm);
    if (!r->s0 && !r->s1) r->s1 = 1;   /* never all-zero */
}

static uint64_t xs128p(rng_t *r)
{
    uint64_t x = r->s0, y = r->s1;
    r->s0 = y;
    x ^= x << 23;
    r->s1 = x ^ y ^ (x >> 17) ^ (y >> 26);
    return r->s1 + y;
}

double rng_uniform(rng_t *r)
{
    /* top 53 bits -> double in [0,1) */
    return (double)(xs128p(r) >> 11) * (1.0 / 9007199254740992.0);
}

double rng_gauss(rng_t *r)
{
    /* Box-Muller; draw fresh each call (simplicity > speed here). */
    double u1 = rng_uniform(r);
    double u2 = rng_uniform(r);
    if (u1 < 1e-300) u1 = 1e-300;
    return sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2);
}

int64_t rng_thresh_e9(rng_t *r)
{
    return (int64_t)(rng_uniform(r) * 1.0e9);
}

double sim_jitter_ns(rng_t *r, jitter_model_t model, double sigma_ns,
                     double bias_ns, int load_active)
{
    switch (model) {
    case JITTER_GAUSS:
        return rng_gauss(r) * sigma_ns;
    case JITTER_HEAVY_TAIL: {
        double base = rng_gauss(r) * sigma_ns;
        if (rng_uniform(r) < 0.05)                 /* 5% outliers at x10 sigma */
            base += rng_gauss(r) * sigma_ns * 10.0;
        return base;
    }
    case JITTER_LOAD_DEP: {
        double s = load_active ? sigma_ns * 4.0 : sigma_ns;  /* load inflates the floor */
        return rng_gauss(r) * s;
    }
    case JITTER_BIASED:
        return rng_gauss(r) * sigma_ns + bias_ns;            /* constant per-node bias */
    }
    return rng_gauss(r) * sigma_ns;
}

double sim_drift_ppm(drift_model_t model, double t_s, double runtime_s)
{
    switch (model) {
    case DRIFT_NONE: return 0.0;
    case DRIFT_SINE: return 3.0 * sin(6.283185307179586 * t_s / 600.0);  /* +-3 ppm, 10 min */
    case DRIFT_RAMP: return (runtime_s > 0.0) ? 2.0 * (t_s / runtime_s) : 0.0; /* +2 ppm */
    }
    return 0.0;
}
