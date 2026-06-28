/* sync_core.c - firmware-faithful sync + sample-clock core. See sync_core.h.
 *
 * Integer arithmetic throughout (the firmware Loop-A path is integer; Loop B is
 * implemented in integer Bresenham so the whole core stays FPU-optional and
 * bit-reproducible on PC and MCU).
 */
#include "sync_core.h"

void sc_init(sc_node_t *n, sc_dither_mode_t dither_mode)
{
    n->s_offset_ns   = 0;
    n->s_rate_ppb    = 0;
    n->s_lastSyncRaw = 0;
    n->s_lastInterval= 0;
    n->s_synced      = 0;
    n->s_syncCount   = 0;
    n->dither_mode   = dither_mode;
    n->per_resid     = 0;
    n->noise_thresh  = SC_ONE_E9 / 2;   /* harmless default until first draw */
    n->sample_k      = 0;
}

/* ns per tick = 125/12 (hwclk_cli.c hwclock_now_ns: tc2_read64()*125/12). */
int64_t sc_ticks_to_ns(int64_t ticks) { return ticks * SC_TICK_NUM / SC_TICK_DEN; }
int64_t sc_ns_to_ticks(int64_t ns)    { return ns * SC_TICK_DEN / SC_TICK_NUM; }

/* ntp_rate_held (ntp_sync.c:68-71): overflow-safe integer scaling.
 * = ((raw - lastSyncRaw)/1000) * rate_ppb / 1e6  = elapsed_s * rate_ppb (ns). */
int64_t sc_rate_held(const sc_node_t *n, uint64_t raw_ns)
{
    return ((int64_t)((raw_ns - n->s_lastSyncRaw) / 1000ULL) * n->s_rate_ppb) / 1000000;
}

/* ntp_now_ns (ntp_sync.c:111-114). */
int64_t sc_now_ns(const sc_node_t *n, uint64_t raw_ns)
{
    return (int64_t)raw_ns + n->s_offset_ns + sc_rate_held(n, raw_ns);
}

/* OP_SET_OFFSET handler (ntp_sync.c:344-367), update order preserved exactly:
 *   1) freeze the rate accrued so far into the phase (continuity)
 *   2) record the interval, re-base the rate accrual at raw
 *   3) I term: integrate adjust/interval (ppb) into s_rate_ppb, Ki=1/4
 *      - skipped on the first sync (s_synced==0) so the epoch jump never feeds I
 *   4) P term: apply the phase correction now, Kp=1
 */
void sc_apply_offset(sc_node_t *n, int64_t adjust, uint64_t raw_ns)
{
    n->s_offset_ns += sc_rate_held(n, raw_ns);                 /* 1 */
    if (n->s_synced)
        n->s_lastInterval = (int64_t)(raw_ns - n->s_lastSyncRaw);
    n->s_lastSyncRaw = raw_ns;                                 /* 2 */

    if (n->s_synced && n->s_lastInterval > 0) {                /* 3 (I) */
        int64_t iv_us = n->s_lastInterval / 1000;
        if (iv_us > 0) {
            int64_t drift_ppb = (adjust * 1000000LL) / iv_us; /* ns/interval -> ppb */
            n->s_rate_ppb += drift_ppb / SC_KI_DEN;
        }
    }

    n->s_offset_ns += adjust;                                  /* 4 (P, Kp=1) */
    n->s_synced = 1;
    n->s_syncCount++;
}

/* Loop B: ideal sample period = PER_NOM*(1 - s_rate_ppb/1e9) ticks (header note #1).
 * Scaled by 1e9 to keep it integer: per_e9 = PER_NOM*1e9 - PER_NOM*s_rate_ppb.
 * Sigma-delta dither the fractional tick so the *mean* period equals per_ideal. */
uint32_t sc_next_period(sc_node_t *n, int64_t rnd_thresh_e9)
{
    int64_t per_e9 = SC_PER_NOM * SC_ONE_E9 - SC_PER_NOM * n->s_rate_ppb;
    int64_t base   = per_e9 / SC_ONE_E9;
    int64_t frac   = per_e9 - base * SC_ONE_E9;   /* in [0,1e9) for plausible ppm */

    n->per_resid += frac;

    int64_t threshold = SC_ONE_E9;
    if (n->dither_mode == SC_DITHER_NOISE) {
        /* ⚠C4: randomise the carry threshold so the carry pattern is aperiodic
         * (breaks Bresenham spurious tones). Mean carry rate is unchanged. */
        n->noise_thresh = rnd_thresh_e9;
        threshold = n->noise_thresh;
    }

    int64_t per_this = base;
    if (n->per_resid >= threshold) {
        per_this += 1;
        n->per_resid -= SC_ONE_E9;   /* always remove one whole tick of credit */
    }

    n->sample_k++;
    return (uint32_t)per_this;
}
