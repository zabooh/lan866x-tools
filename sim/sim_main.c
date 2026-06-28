/* sim_main.c - PC-only simulation shell for the sync-ADC feasibility study.
 *
 * MILESTONE STATUS: M4 (N nodes, common start, the core feasibility question).
 * Adds the GO/index anchor (concept §5.1): at go_at_s the master broadcasts
 * X = master_now + lead; each node computes its local raw-tick where its
 * disciplined clock reads X and starts sampling there with sample_k=0. The start
 * instants differ by each node's clock-sync error (~us) - that is the realistic
 * anchor. We then track each node's CONTINUOUS sample index over real time and
 * log pairwise skew. ACCEPTANCE: |index_skew_samples| < 1 for every pair, for the
 * whole run. Stress models / faults / sweep are M5-M7.
 *
 * All clock/PI/dither logic lives in sync_core.{h,c}; nothing duplicated here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "sim_config.h"
#include "sync_core.h"
#include "sim_noise.h"

#define DITHER_LOG_MAX 65536
#define BIG_T          1e300

typedef struct {
    sc_node_t core;
    double    ppm_base;
    rng_t     rng;
    double    raw_ticks;
    int64_t   last_T;
    /* Loop A */
    double    lock_time_s; int locked;
    /* Loop B sampling */
    double    next_samp_T;     /* real-tick time of next sample (BIG_T before GO)  */
    double    last_samp_T;     /* real-tick time of the most recent sample         */
    double    last_real_dt;    /* its real duration (for continuous-index interp)  */
    long      samp_total;
    double    bias_ns;         /* per-node constant offset bias (⚠A3 positional)   */
    /* M6 fault + consistency-check state */
    int       skip_samples;    /* remaining samples to drop (FAULT_SAMPLE_LOSS)    */
    long      lost_total;
    int       hb_flagged;      /* ever flagged by the master consistency check     */
    double    hb_max_skew;     /* worst |reported-expected| seen [samples]         */
    /* back-channel certification state */
    double    adj_ema, adj2_ema; int adj_init;  /* local sync-jitter estimate (std of adjust) */
    double    bc_skew_ema; int bc_init;         /* master's averaged skew estimate [samples]  */
    int       master_confirmed;                 /* back-channel verdict relayed to the node    */
    double    gt_sum_abs, gt_max; long gt_n;    /* ground-truth abs skew vs master axis (steady)*/
} sim_node_t;

static const char *jitter_name(jitter_model_t m){switch(m){case JITTER_GAUSS:return"gauss";
    case JITTER_HEAVY_TAIL:return"heavy_tail";case JITTER_LOAD_DEP:return"load_dep";
    case JITTER_BIASED:return"biased";}return"?";}
static const char *dither_name(sc_dither_mode_t m){return m==SC_DITHER_NOISE?"noise":"bresenham";}
static const char *drift_name(drift_model_t m){switch(m){case DRIFT_NONE:return"none";
    case DRIFT_SINE:return"sine";case DRIFT_RAMP:return"ramp";}return"?";}

static int parse_jitter(const char*s,jitter_model_t*o){
    if(!strcmp(s,"gauss")){*o=JITTER_GAUSS;return 0;} if(!strcmp(s,"heavy_tail")){*o=JITTER_HEAVY_TAIL;return 0;}
    if(!strcmp(s,"load_dep")){*o=JITTER_LOAD_DEP;return 0;} if(!strcmp(s,"biased")){*o=JITTER_BIASED;return 0;}
    return -1; }
static int parse_drift(const char*s,drift_model_t*o){
    if(!strcmp(s,"none")){*o=DRIFT_NONE;return 0;} if(!strcmp(s,"sine")){*o=DRIFT_SINE;return 0;}
    if(!strcmp(s,"ramp")){*o=DRIFT_RAMP;return 0;} return -1; }
static int parse_dither(const char*s,sc_dither_mode_t*o){
    if(!strcmp(s,"bresenham")){*o=SC_DITHER_BRESENHAM;return 0;} if(!strcmp(s,"noise")){*o=SC_DITHER_NOISE;return 0;}
    return -1; }
static int parse_fault(const char*s,fault_kind_t*o){
    if(!strcmp(s,"none")){*o=FAULT_NONE;return 0;} if(!strcmp(s,"go_loss")){*o=FAULT_GO_LOSS;return 0;}
    if(!strcmp(s,"reboot")){*o=FAULT_REBOOT;return 0;} if(!strcmp(s,"sample_loss")){*o=FAULT_SAMPLE_LOSS;return 0;}
    return -1; }

static int64_t master_ns(int64_t T){ return sc_ticks_to_ns(T); }

static void node_advance(sim_node_t *n, int64_t T, double t_s, const sim_config_t *c)
{
    if (T <= n->last_T) return;
    double dt  = (double)(T - n->last_T);
    double ppm = n->ppm_base + sim_drift_ppm(c->drift_model, t_s, c->runtime_s);
    n->raw_ticks += dt * (1.0 + ppm * 1e-6);
    n->last_T = T;
}
static uint64_t node_raw_ns(const sim_node_t *n)
{ return (uint64_t)sc_ticks_to_ns((int64_t)(n->raw_ticks + 0.5)); }
static double node_ppm(const sim_node_t *n, double t_s, const sim_config_t *c)
{ return n->ppm_base + sim_drift_ppm(c->drift_model, t_s, c->runtime_s); }

/* continuous (fractional) sample index of a node at real time T (NAN if not started) */
static double idx_cont(const sim_node_t *n, double T)
{
    if (n->core.sample_k == 0) return NAN;
    return (double)(n->core.sample_k - 1) + (T - n->last_samp_T) / n->last_real_dt;
}

static int cmp_double(const void *a,const void *b)
{ double d=*(const double*)a-*(const double*)b; return (d>0)-(d<0); }

static void master_sync_node(sim_node_t *n, int64_t T, const sim_config_t *c, int load_active)
{
    int64_t raw = node_raw_ns(n);
    int64_t true_off = sc_now_ns(&n->core, raw) - master_ns(T);
    int R=c->rounds_R; if(R>64)R=64; if(R<1)R=1;
    /* LEVER: dedicated sync slot -> the sync exchange is not behind data frames,
     * so its jitter is NOT inflated under load (vs load_dep). */
    int sync_load = c->sync_slot ? 0 : load_active;
    /* LEVER: per-node bias calibration -> residual bias = bias * bias_cal (vs A3). */
    double eff_bias = n->bias_ns * c->bias_cal;
    double off[64],dly[64];
    for(int r=0;r<R;r++){
        off[r]=(double)true_off+sim_jitter_ns(&n->rng,c->jitter_model,c->sigma_ns,eff_bias,sync_load);
        dly[r]=c->sigma_ns+fabs(rng_gauss(&n->rng))*c->sigma_ns;
    }
    int K=R/2; if(K<1)K=1;
    int idx[64]; for(int i=0;i<R;i++)idx[i]=i;
    for(int i=0;i<K;i++)for(int j=i+1;j<R;j++)
        if(dly[idx[j]]<dly[idx[i]]){int t=idx[i];idx[i]=idx[j];idx[j]=t;}
    double sel[64]; for(int i=0;i<K;i++)sel[i]=off[idx[i]];
    qsort(sel,(size_t)K,sizeof(double),cmp_double);
    /* LEVER: outlier gating -> drop rounds beyond k*MAD of the median before the
     * final median (vs heavy_tail spikes). */
    if(c->outlier_k>0.0 && K>=3){
        double med0=sel[K/2];
        double dev[64]; for(int i=0;i<K;i++)dev[i]=fabs(sel[i]-med0);
        qsort(dev,(size_t)K,sizeof(double),cmp_double);
        double mad=dev[K/2]; double lim=c->outlier_k*1.4826*mad;
        if(lim>0){ double keep[64]; int nk=0;
            for(int i=0;i<K;i++) if(fabs(sel[i]-med0)<=lim) keep[nk++]=sel[i];
            if(nk>=1){ for(int i=0;i<nk;i++)sel[i]=keep[i]; K=nk; } }
    }
    double robust=(K&1)?sel[K/2]:0.5*(sel[K/2-1]+sel[K/2]);
    int64_t adjust=-(int64_t)robust;
    sc_apply_offset(&n->core,adjust,(uint64_t)raw);
    /* local sync-jitter estimate: EMA mean/var of the post-lock 'adjust' stream.
     * std(adjust) ~ the node's own sync sigma -> local self-assessment of per-sample
     * uncertainty (necessary, not sufficient: blind to a constant anchor error). */
    if(n->locked){ double a=(double)adjust;
        if(!n->adj_init){n->adj_ema=a;n->adj2_ema=a*a;n->adj_init=1;}
        else{n->adj_ema=0.1*a+0.9*n->adj_ema; n->adj2_ema=0.1*a*a+0.9*n->adj2_ema;} }
    if(!n->locked){ double tg=-n->ppm_base*1000.0;
        if(fabs((double)n->core.s_rate_ppb-tg)<0.10*fabs(tg)){n->locked=1;n->lock_time_s=(double)T/(double)SC_HW_HZ;} }
}

/* GO: anchor every node's sample index 0 to NTP time X (concept §5.1).
 * X_ns is the master's anchor (returned via *X_out for the consistency check).
 * A FAULT_GO_LOSS node anchors to a WRONG instant (X + offset) -> its index is
 * permanently shifted vs the master axis (⚠D1). */
static void do_go(sim_node_t *nodes, const sim_config_t *c, int64_t T_go, int64_t *X_out)
{
    double t_s = (double)T_go/(double)SC_HW_HZ;
    int64_t X_ns = master_ns(T_go) + sc_ticks_to_ns(c->startup_lead_ticks);
    *X_out = X_ns;
    for(int i=0;i<c->n_nodes;i++){
        node_advance(&nodes[i],T_go,t_s,c);
        int64_t Xeff = X_ns;
        if(c->fault==FAULT_GO_LOSS && i==c->fault_node)
            Xeff += (int64_t)(c->fault_go_off_us*1000.0);   /* mis-anchored start */
        int64_t off = nodes[i].core.s_offset_ns;
        int64_t raw0 = Xeff - off;
        int64_t rh = (int64_t)((((uint64_t)raw0 - nodes[i].core.s_lastSyncRaw)/1000ULL)
                               * nodes[i].core.s_rate_ppb)/1000000;
        int64_t target_raw_ns = Xeff - off - rh;
        double  target_raw_ticks = (double)sc_ns_to_ticks(target_raw_ns);
        double  ppm = node_ppm(&nodes[i],t_s,c);
        nodes[i].next_samp_T = target_raw_ticks/(1.0+ppm*1e-6);
    }
}

/* Master index-consistency check (concept §8.1, ⚠D1). Each node "heartbeats"
 * (index, ntp-time-of-index); the master verifies the index matches the time:
 *   expected = (ntp - X)/125us ;  flag if |reported - expected| > tol.
 * Returns 1 if this node is flagged inconsistent. */
static int heartbeat_check(sim_node_t *n, int64_t X_ns, const sim_config_t *c,
                           double samp_ns, double *skew_out, double *exp_out)
{
    uint64_t raw = node_raw_ns(n);
    int64_t  ntp = sc_now_ns(&n->core, raw);
    double reported = (double)((long long)n->core.sample_k - 1);  /* 0-based index   */
    double expected = (double)(ntp - X_ns) / samp_ns;
    double skew = reported - expected;
    *skew_out = skew; *exp_out = expected;
    if (fabs(skew) > n->hb_max_skew) n->hb_max_skew = fabs(skew);
    return fabs(skew) > (double)c->hb_tol_samples;
}

static FILE *open_out(const sim_config_t *c,const char*name)
{ char p[512]; snprintf(p,sizeof p,"%s/%s",c->out_dir,name);
  FILE*f=fopen(p,"w"); if(!f)fprintf(stderr,"cannot open %s\n",p); return f; }

static int run(const sim_config_t *c)
{
    rng_t mr; rng_seed(&mr,c->seed);
    int64_t per_nom = SC_HW_HZ / c->sample_hz;          /* sample period in ticks   */
    double  samp_ns = 1.0e9 / (double)c->sample_hz;     /* sample period in ns      */
    sim_node_t *nd=(sim_node_t*)calloc((size_t)c->n_nodes,sizeof *nd);
    if(!nd){fprintf(stderr,"OOM\n");return 1;}
    for(int i=0;i<c->n_nodes;i++){
        sc_init(&nd[i].core,c->dither_mode);
        nd[i].ppm_base=c->ppm_base_lo+rng_uniform(&mr)*(c->ppm_base_hi-c->ppm_base_lo);
        rng_seed(&nd[i].rng,c->seed*1000003u+(uint64_t)(i+1));
        nd[i].core.kp=c->kp;                 /* proposed phase-gain knob (1.0=firmware) */
        nd[i].core.ki_den=c->ki_den;         /* proposed integral smoothing (4=firmware) */
        nd[i].core.per_nom=per_nom;          /* sample rate knob (12000=8kHz, 24000=4kHz) */
        nd[i].raw_ticks=0; nd[i].last_T=0; nd[i].lock_time_s=-1; nd[i].locked=0;
        nd[i].next_samp_T=BIG_T; nd[i].last_samp_T=0; nd[i].last_real_dt=(double)per_nom; nd[i].samp_total=0;
        /* per-node positional bias (⚠A3): differs per node so it does NOT cancel
         * in pairwise; only the common part would. Drawn deterministically. */
        nd[i].bias_ns = (2.0*rng_uniform(&mr)-1.0) * c->bias_ns;
    }

    int det=c->detail_csv;
    FILE *ts=det?open_out(c,"run_timeseries.csv"):NULL;
    FILE *pw=det?open_out(c,"run_pairwise.csv"):NULL;
    FILE *dj=det?open_out(c,"dither_jitter.csv"):NULL;
    FILE *fc=det?open_out(c,"run_faultcheck.csv"):NULL;
    if(det && (!ts||!pw||!dj||!fc)){free(nd);return 1;}
    if(ts)fprintf(ts,"t_ms,node_id,s_offset_ns,s_rate_ppb,sample_k,true_ppm,ntp_err_ns\n");
    if(pw)fprintf(pw,"t_ms,node_a,node_b,index_skew_samples,time_skew_ns\n");
    if(dj)fprintf(dj,"sample_k,period_ticks\n");
    if(fc)fprintf(fc,"t_ms,node_id,reported_index,expected_index,skew_samples,flagged\n");

    double T_end=c->runtime_s*(double)SC_HW_HZ;
    double log_dt=(c->ts_sample_ms/1000.0)*(double)SC_HW_HZ; if(log_dt<1)log_dt=1;
    double sync_dt=(double)c->sync_interval_ticks;
    double T_go=c->go_at_s*(double)SC_HW_HZ;
    double hb_dt=1.0*(double)SC_HW_HZ;                 /* heartbeat check every 1 s */
    double T_fault=(c->fault!=FAULT_NONE)?c->fault_time_s*(double)SC_HW_HZ:BIG_T;
    double next_log=0,next_sync=sync_dt,next_hb=BIG_T; int go_done=0,fault_done=0;
    int64_t X_ns=0; long dj_rows=0; long hb_total=0,hb_flagged_rows=0;

    /* steady-window pairwise stats */
    double max_skew=0.0, max_tskew=0.0; double sum_tsq=0.0; long ts_n=0;

    for(;;){
        double te=next_log; if(next_sync<te)te=next_sync;
        if(!go_done && T_go<te) te=T_go;
        if(next_hb<te) te=next_hb;
        if(!fault_done && T_fault<te) te=T_fault;
        for(int i=0;i<c->n_nodes;i++) if(nd[i].next_samp_T<te) te=nd[i].next_samp_T;
        if(te>T_end) break;
        double t_s=te/(double)SC_HW_HZ; int64_t Ti=(int64_t)(te+0.5);

        if(!go_done && T_go<=te){ do_go(nd,c,(int64_t)(T_go+0.5),&X_ns); go_done=1;
            next_hb=te+hb_dt; }

        /* fault injection (one-shot) */
        if(!fault_done && T_fault<=te){
            int fn=c->fault_node;
            if(fn>=0 && fn<c->n_nodes){
                node_advance(&nd[fn],Ti,t_s,c);
                if(c->fault==FAULT_SAMPLE_LOSS){
                    nd[fn].skip_samples=c->fault_samples;     /* drop J, don't count */
                } else if(c->fault==FAULT_REBOOT){
                    /* reset clock state; rejoin index from the pre-lock (offset=0)
                     * clock -> wrong anchor (⚠D4). raw keeps running. */
                    sc_dither_mode_t dm=nd[fn].core.dither_mode;
                    uint64_t raw=node_raw_ns(&nd[fn]);
                    sc_init(&nd[fn].core,dm);                 /* sample_k=0, offset/rate=0 */
                    int64_t join=(int64_t)(((int64_t)raw - X_ns)/(int64_t)samp_ns); /* offset=0 -> wrong */
                    if(join<0) join=0;
                    nd[fn].core.sample_k=(uint64_t)(join+1);
                }
            }
            fault_done=1;
        }

        if(next_sync<=te){ for(int i=0;i<c->n_nodes;i++){node_advance(&nd[i],Ti,t_s,c);
            master_sync_node(&nd[i],Ti,c,/*load_active=*/go_done);} next_sync+=sync_dt; }

        if(next_log<=te){
            for(int i=0;i<c->n_nodes;i++){ node_advance(&nd[i],Ti,t_s,c);
                uint64_t raw=node_raw_ns(&nd[i]);
                int64_t err=sc_now_ns(&nd[i].core,raw)-master_ns(Ti);
                if(ts)fprintf(ts,"%.3f,%d,%lld,%lld,%llu,%.3f,%lld\n",t_s*1000.0,i,
                    (long long)nd[i].core.s_offset_ns,(long long)nd[i].core.s_rate_ppb,
                    (unsigned long long)nd[i].core.sample_k,node_ppm(&nd[i],t_s,c),(long long)err);
            }
            /* pairwise skew (continuous index), once sampling has started */
            for(int a=0;a<c->n_nodes;a++)for(int b=a+1;b<c->n_nodes;b++){
                double ia=idx_cont(&nd[a],te), ib=idx_cont(&nd[b],te);
                if(isnan(ia)||isnan(ib)) continue;
                double skew=ia-ib; double tskew=skew*samp_ns;
                if(pw)fprintf(pw,"%.3f,%d,%d,%.6f,%.3f\n",t_s*1000.0,a,b,skew,tskew);
                if(t_s>c->runtime_s*0.5){
                    if(fabs(skew)>max_skew)max_skew=fabs(skew);
                    if(fabs(tskew)>max_tskew)max_tskew=fabs(tskew);
                    sum_tsq+=tskew*tskew; ts_n++;
                }
            }
            next_log+=log_dt;
        }

        if(next_hb<=te){
            for(int i=0;i<c->n_nodes;i++){
                node_advance(&nd[i],Ti,t_s,c);
                if(nd[i].core.sample_k==0) continue;     /* not sampling yet */
                double skew,expd; int fl=heartbeat_check(&nd[i],X_ns,c,samp_ns,&skew,&expd);
                if(fc)fprintf(fc,"%.3f,%d,%lld,%.3f,%.3f,%d\n",t_s*1000.0,i,
                        (long long)nd[i].core.sample_k-1,expd,skew,fl);
                hb_total++; if(fl){hb_flagged_rows++; nd[i].hb_flagged=1;}

                /* ---- back-channel certification ----
                 * The node heartbeats (index, its NTP time). The master places the
                 * index on its OWN clock axis (ground-truth reference here), so the
                 * check does NOT trust the node's clock -> catches clock AND anchor
                 * errors. The heartbeat carries the same software-timestamp jitter
                 * (sigma), so the master AVERAGES (EMA) -> a constant anchor error
                 * survives the averaging, the jitter beats down. The verdict is
                 * relayed back; the node only trusts "in sync" when CONFIRMED. */
                double skew_true = idx_cont(&nd[i],te)
                                 - (double)(master_ns(Ti)-X_ns)/samp_ns;  /* continuous */
                double meas = skew_true + rng_gauss(&nd[i].rng)*(c->sigma_ns/samp_ns);
                if(!nd[i].bc_init){nd[i].bc_skew_ema=meas;nd[i].bc_init=1;}
                else nd[i].bc_skew_ema = 0.2*meas + 0.8*nd[i].bc_skew_ema;
                int anchor_ok = fabs(nd[i].bc_skew_ema) < 1.0;   /* anchor tol = 1 sample */
                nd[i].master_confirmed = anchor_ok && nd[i].locked;

                if(t_s>c->runtime_s*0.5){
                    double a=fabs(skew_true);
                    nd[i].gt_sum_abs+=a; if(a>nd[i].gt_max)nd[i].gt_max=a; nd[i].gt_n++;
                }
            }
            next_hb+=hb_dt;
        }

        for(int i=0;i<c->n_nodes;i++){
            if(nd[i].next_samp_T<=te){
                node_advance(&nd[i],Ti,t_s,c);
                if(nd[i].skip_samples>0){                 /* DMA overflow: lose sample */
                    nd[i].skip_samples--; nd[i].lost_total++;
                    nd[i].next_samp_T=te+nd[i].last_real_dt;  /* time advances, index does NOT */
                    continue;
                }
                int64_t thr=(c->dither_mode==SC_DITHER_NOISE)?rng_thresh_e9(&nd[i].rng):0;
                uint32_t per=sc_next_period(&nd[i].core,thr);
                double ppm=node_ppm(&nd[i],t_s,c);
                double real_dt=(double)per/(1.0+ppm*1e-6);
                nd[i].last_samp_T=te; nd[i].last_real_dt=real_dt;
                nd[i].next_samp_T=te+real_dt; nd[i].samp_total++;
                if(dj&&i==0&&dj_rows<DITHER_LOG_MAX){
                    fprintf(dj,"%llu,%u\n",(unsigned long long)nd[0].core.sample_k,per); dj_rows++; }
            }
        }
    }
    if(ts){fclose(ts);}
    if(pw){fclose(pw);}
    if(dj){fclose(dj);}
    if(fc){fclose(fc);}

    double rms_tskew = ts_n? sqrt(sum_tsq/(double)ts_n):0.0;

    printf("\n node | ppm_base | lock@[s] | samples | start@[ms]\n");
    printf("------+----------+----------+---------+-----------\n");
    for(int i=0;i<c->n_nodes;i++){
        char lb[16]; if(nd[i].locked)snprintf(lb,sizeof lb,"%.3f",nd[i].lock_time_s);else snprintf(lb,sizeof lb,"no");
        /* recover each node's first-sample real time from samp_total/last (approx): */
        printf(" %4d | %8.3f | %8s | %7ld |  (GO+%.0fms)\n",
               i,nd[i].ppm_base,lb,nd[i].samp_total,c->startup_lead_ticks*1000.0/(double)SC_HW_HZ);
    }
    printf("\nGO at %.2fs (X = +%.0fms lead).  pairwise pairs=%d\n",
           c->go_at_s, c->startup_lead_ticks*1000.0/(double)SC_HW_HZ, c->n_nodes*(c->n_nodes-1)/2);
    printf("STEADY (t>%.0fs): max |index_skew| = %.6f samples   max |time_skew| = %.3f us   RMS time_skew = %.3f us\n",
           c->runtime_s*0.5, max_skew, max_tskew/1000.0, rms_tskew/1000.0);
    printf(" -> %s\n", (max_skew<1.0)?"INDEX-ALIGNED < 1 sample (M4 PASS)":"SKEW >= 1 SAMPLE (concept broken here)");

    /* ---- M6 fault-consistency report ---- */
    if(c->fault!=FAULT_NONE){
        const char *fn = c->fault==FAULT_GO_LOSS?"GO_LOSS":
                         c->fault==FAULT_REBOOT?"REBOOT":"SAMPLE_LOSS";
        printf("\nFAULT: %s on node %d @ %.1fs   (heartbeat checks: %ld, flagged rows: %ld, tol=%d smp)\n",
               fn, c->fault_node, c->fault_time_s, hb_total, hb_flagged_rows, c->hb_tol_samples);
        printf(" node | flagged | worst |skew| [smp] | lost samples\n");
        printf("------+---------+------------------+-------------\n");
        for(int i=0;i<c->n_nodes;i++)
            printf(" %4d |   %s   | %16.3f | %ld\n",
                   i, nd[i].hb_flagged?"YES":" no", nd[i].hb_max_skew, nd[i].lost_total);
        int only_faulty = nd[c->fault_node].hb_flagged;
        int others_clean = 1;
        for(int i=0;i<c->n_nodes;i++) if(i!=c->fault_node && nd[i].hb_flagged) others_clean=0;
        printf(" -> %s\n", (only_faulty&&others_clean)
               ? "CHECK CAUGHT the faulty node, others clean (M6 PASS)"
               : "check did NOT cleanly isolate the fault");
    }

    /* ---- back-channel certification report ----
     * Compares two ways for the system to decide "am I really in sync":
     *   local-only  : locked AND own sync-jitter sigma below the break threshold
     *                 (necessary, but BLIND to a constant anchor error)
     *   back-channel: master confirms the index is correctly anchored on the
     *                 master axis (catches the silent anchor faults too).
     * "Truly in sync" ground truth = mean |skew vs master axis| < 1 sample. A
     * certification that says IN SYNC while truly-out is a dangerous FALSE POSITIVE. */
    {
        printf("\n=== back-channel certification (\"am I really in sync?\") ===\n");
        printf(" node | local_sigma | actual mean|skew| | actual max | local | master | \n");
        printf("------+-------------+-------------------+------------+-------+--------+\n");
        int fp_local=0, fp_master=0, fn_local=0, fn_master=0;
        for(int i=0;i<c->n_nodes;i++){
            double var=nd[i].adj2_ema-nd[i].adj_ema*nd[i].adj_ema; if(var<0)var=0;
            double lsig=sqrt(var);                         /* local sync sigma [ns]  */
            int cert_local  = nd[i].locked && lsig < 0.264*samp_ns; /* break threshold ~σ */
            int cert_master = nd[i].master_confirmed;
            double amean = nd[i].gt_n? nd[i].gt_sum_abs/(double)nd[i].gt_n : 0.0;
            int truly_in = amean < 1.0;                    /* correctly anchored      */
            if(cert_local  && !truly_in) fp_local++;
            if(cert_master && !truly_in) fp_master++;
            if(!cert_local  && truly_in) fn_local++;
            if(!cert_master && truly_in) fn_master++;
            printf(" %4d | %8.1f ns | %17.3f | %10.3f | %5s | %6s |%s\n",
                   i, lsig, amean, nd[i].gt_max,
                   cert_local?"IN":"out", cert_master?"IN":"out",
                   (cert_local&&!truly_in)?"  <- local FALSE POSITIVE":"");
        }
        printf("\n local-only : false-positives=%d  false-negatives=%d\n", fp_local, fn_local);
        printf(" back-chan  : false-positives=%d  false-negatives=%d  -> %s\n",
               fp_master, fn_master,
               fp_master==0 ? "reliable (no node wrongly certifies in-sync)" : "UNRELIABLE");
        printf(" note: \"IN\" = correctly anchored & locked. Per-sample jitter is still\n");
        printf("       ~sigma/125us samples (report it separately); at high sigma a node can be\n");
        printf("       correctly anchored (master IN) yet have several-sample instantaneous skew.\n");
    }

    /* summary: --append opens "a" and writes the header only if the file is new
     * (so a sweep accumulates one row per run; M7 driver relies on this). */
    {
        char path[512]; snprintf(path,sizeof path,"%s/run_summary.csv",c->out_dir);
        int need_hdr = 1;
        if(c->append){ FILE *t=fopen(path,"r"); if(t){ fseek(t,0,SEEK_END); if(ftell(t)>0) need_hdr=0; fclose(t);} }
        FILE *sm=fopen(path, c->append?"a":"w");
        if(sm){ double worst_lock=0; int all_locked=1;
            for(int i=0;i<c->n_nodes;i++){if(!nd[i].locked)all_locked=0;else if(nd[i].lock_time_s>worst_lock)worst_lock=nd[i].lock_time_s;}
            if(need_hdr) fprintf(sm,"seed,jitter_model,sigma,n_nodes,runtime_s,dither_mode,drift_model,"
                "max_index_skew,rms_time_skew_ns,lock_time_s,stable\n");
            fprintf(sm,"%llu,%s,%.0f,%d,%.1f,%s,%s,%.6f,%.1f,%.3f,%d\n",
                (unsigned long long)c->seed,jitter_name(c->jitter_model),c->sigma_ns,
                c->n_nodes,c->runtime_s,dither_name(c->dither_mode),drift_name(c->drift_model),
                max_skew,rms_tskew,worst_lock,(all_locked&&max_skew<1.0));
            fclose(sm);}
    }
    free(nd);
    return 0;
}

static void usage(const char*p){
    printf("usage: %s [options]\n",p);
    printf("  --nodes N     --runtime S    --seed K      --sigma NS    --tslog MS   --goat S\n");
    printf("  --jitter gauss|heavy_tail|load_dep|biased  --drift none|sine|ramp\n");
    printf("  --dither bresenham|noise   --bias NS   --rounds R   --append   --out DIR\n");
    printf("  --fault none|go_loss|reboot|sample_loss  --faultnode N  --faulttime S  --faultsamples J\n");
    printf("  --kp K  --kiden D   PROPOSED controller knobs (default 1.0 / 4 = firmware; REPORT.md §5)\n");
    printf("  --samplehz HZ       sampling rate (8000=default; 4000=half-rate knob)\n");
    printf("  --syncms MS         sync interval in ms (125=default; 62.5=double-rate knob)\n");
    printf("  --syncslot          dedicated sync window (sync not inflated under load; vs load_dep)\n");
    printf("  --outlierk K        reject rounds beyond K*MAD before median (vs heavy_tail)\n");
    printf("  --biascal R         residual per-node bias after calibration (1.0=none, 0.1=90%%; vs biased)\n");
    printf("  --summaryonly       skip per-run detail CSVs (fast sweeps)\n");
}
static int parse_args(int argc,char**argv,sim_config_t*c){
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--help")){usage(argv[0]);return 1;}
        else if(!strcmp(argv[i],"--nodes")&&i+1<argc)c->n_nodes=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--runtime")&&i+1<argc)c->runtime_s=atof(argv[++i]);
        else if(!strcmp(argv[i],"--seed")&&i+1<argc)c->seed=(uint64_t)strtoull(argv[++i],0,10);
        else if(!strcmp(argv[i],"--sigma")&&i+1<argc)c->sigma_ns=atof(argv[++i]);
        else if(!strcmp(argv[i],"--bias")&&i+1<argc)c->bias_ns=atof(argv[++i]);
        else if(!strcmp(argv[i],"--rounds")&&i+1<argc)c->rounds_R=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--tslog")&&i+1<argc)c->ts_sample_ms=atof(argv[++i]);
        else if(!strcmp(argv[i],"--goat")&&i+1<argc)c->go_at_s=atof(argv[++i]);
        else if(!strcmp(argv[i],"--out")&&i+1<argc)c->out_dir=argv[++i];
        else if(!strcmp(argv[i],"--append"))c->append=1;
        else if(!strcmp(argv[i],"--summaryonly"))c->detail_csv=0;
        else if(!strcmp(argv[i],"--kp")&&i+1<argc)c->kp=atof(argv[++i]);
        else if(!strcmp(argv[i],"--kiden")&&i+1<argc)c->ki_den=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--samplehz")&&i+1<argc)c->sample_hz=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--syncms")&&i+1<argc)c->sync_interval_ticks=(int64_t)(atof(argv[++i])/1000.0*(double)SC_HW_HZ);
        else if(!strcmp(argv[i],"--syncslot"))c->sync_slot=1;
        else if(!strcmp(argv[i],"--outlierk")&&i+1<argc)c->outlier_k=atof(argv[++i]);
        else if(!strcmp(argv[i],"--biascal")&&i+1<argc)c->bias_cal=atof(argv[++i]);
        else if(!strcmp(argv[i],"--jitter")&&i+1<argc){ if(parse_jitter(argv[++i],&c->jitter_model)){fprintf(stderr,"bad --jitter\n");return -1;} }
        else if(!strcmp(argv[i],"--drift")&&i+1<argc){ if(parse_drift(argv[++i],&c->drift_model)){fprintf(stderr,"bad --drift\n");return -1;} }
        else if(!strcmp(argv[i],"--dither")&&i+1<argc){ if(parse_dither(argv[++i],&c->dither_mode)){fprintf(stderr,"bad --dither\n");return -1;} }
        else if(!strcmp(argv[i],"--fault")&&i+1<argc){ if(parse_fault(argv[++i],&c->fault)){fprintf(stderr,"bad --fault\n");return -1;} }
        else if(!strcmp(argv[i],"--faultnode")&&i+1<argc)c->fault_node=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--faulttime")&&i+1<argc)c->fault_time_s=atof(argv[++i]);
        else if(!strcmp(argv[i],"--faultsamples")&&i+1<argc)c->fault_samples=atoi(argv[++i]);
        else{fprintf(stderr,"unknown arg: %s\n",argv[i]);usage(argv[0]);return -1;}
    }
    return 0;
}

int main(int argc,char**argv){
    sim_config_t cfg=sim_config_default();
    int pr=parse_args(argc,argv,&cfg); if(pr!=0)return pr<0?1:0;
    printf("=== sync-ADC simulation (M7: full; see REPORT.md) ===\n");
    printf("nodes=%d runtime=%.1fs seed=%llu jitter=%s sigma=%.0fns bias=%.0fns R=%d drift=%s dither=%s goat=%.1fs\n",
        cfg.n_nodes,cfg.runtime_s,(unsigned long long)cfg.seed,jitter_name(cfg.jitter_model),
        cfg.sigma_ns,cfg.bias_ns,cfg.rounds_R,drift_name(cfg.drift_model),
        dither_name(cfg.dither_mode),cfg.go_at_s);
    printf("knobs: kp=%.3f ki_den=%d sample=%d Hz (%.0f us/sample) sync=%.1f ms\n",
        cfg.kp,cfg.ki_den,cfg.sample_hz,1e6/(double)cfg.sample_hz,
        (double)cfg.sync_interval_ticks/(double)SC_HW_HZ*1000.0);
    printf("fw constants: Ki=1/%d Kp=%d HW=%lld Hz tick=%lld/%lld ns PER_NOM=%lld\n",
        SC_KI_DEN,SC_KP_NUM,(long long)SC_HW_HZ,(long long)SC_TICK_NUM,
        (long long)SC_TICK_DEN,(long long)SC_PER_NOM);
    return run(&cfg);
}
