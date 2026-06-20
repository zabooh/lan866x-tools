/*
 * tool_common.h  -  tiny shared helper for the pure-C command-line tools:
 *                   discover endpoints, print them, select the target.
 * Each tool defines its own  uint8_t MULTICAST_IP[] = {224,0,0,1};
 */
#ifndef TOOL_COMMON_H
#define TOOL_COMMON_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>
#include "rcp.h"

/* Run discovery for waitS seconds; select the endpoint by IP string (wantIp)
 * or by index (wantEp). Returns the selected index, or -1 on failure. */
/* Scan the currently-known endpoints for the one requested by IP string
 * (wantIp) or by index (wantEp). Returns its index, or -1 if not seen yet. */
static int tool_match_endpoint(const char *wantIp, int wantEp)
{
    rcp_endpoint_t eps[RCP_MAX_ENDPOINTS];
    uint8_t n = rcp_get_endpoints(eps, RCP_MAX_ENDPOINTS);
    int i;
    for (i = 0; i < (int)n; ++i) {
        char s[20];
        snprintf(s, sizeof(s), "%u.%u.%u.%u", eps[i].ip[0], eps[i].ip[1], eps[i].ip[2], eps[i].ip[3]);
        if ((wantIp && !strcmp(wantIp, s)) || (!wantIp && i == wantEp)) return i;
    }
    return -1;
}

/* Run discovery up to waitS seconds, but return the moment the requested
 * endpoint answers (the offer typically arrives in tens of ms) instead of
 * always burning the full window. Select it by IP string (wantIp) or by index
 * (wantEp). Returns the selected index, or -1 on failure. */
static int tool_select(const char *wantIp, int wantEp, int waitS, const char *toolName)
{
    int i, sel = -1;
    printf("%s\nSearching for endpoints (max %d s) ...\n", toolName, waitS);
    if (!rcp_init(NULL)) { printf("rcp_init failed.\n"); return -1; }
    for (i = 0; i < waitS * 10; ++i) {
        rcp_poll();
        sel = tool_match_endpoint(wantIp, wantEp);
        if (sel >= 0) break;        /* target found -> stop waiting */
        Sleep(100);
    }
    if (sel < 0) {
        printf(rcp_is_ready() ? "Target endpoint not found.\n" : "No endpoints found.\n");
        return -1;
    }
    if (!rcp_select_endpoint((uint8_t)sel)) { printf("Target endpoint not found.\n"); return -1; }
    return sel;
}

/* Re-acquire the endpoint after a reboot. The rebooted node may come back at a
 * DIFFERENT IP than before (e.g. the bootloader config's IP differs from the main
 * app's - a real trap when flashing). So instead of polling GetStatus at the old
 * IP, this re-runs discovery and selects whichever endpoint now answers GetStatus,
 * regardless of its IP. Returns the selected index, or -1 if none reappeared
 * within waitS seconds. Leaves the answering endpoint selected. */
static int tool_reacquire(int waitS)
{
    int i, t, sel = -1;
    rcp_set_retries(0); rcp_set_timeout_ms(600);   /* a non-answer is expected while rebooting */
    for (t = 0; t < waitS * 2 && sel < 0; ++t) {   /* ~2 ticks/s */
        rcp_endpoint_t eps[RCP_MAX_ENDPOINTS];
        uint8_t n;
        rcp_poll();                                /* pump SD: collect (re)offers */
        n = rcp_get_endpoints(eps, RCP_MAX_ENDPOINTS);
        for (i = 0; i < (int)n; ++i) {
            GetStatusReply_t st;
            if (!eps[i].available) continue;
            if (!rcp_select_endpoint((uint8_t)i)) continue;
            memset(&st, 0, sizeof(st));
            if (rcp_get_status(&st) == RT_OK) {    /* this one is alive -> take it */
                printf("  endpoint reappeared at %u.%u.%u.%u\n",
                       eps[i].ip[0], eps[i].ip[1], eps[i].ip[2], eps[i].ip[3]);
                sel = i; break;
            }
        }
        if (sel < 0) Sleep(300);
    }
    rcp_set_retries(3); rcp_set_timeout_ms(1500);  /* restore defaults */
    return sel;
}

#endif /* TOOL_COMMON_H */
