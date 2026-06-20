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

#endif /* TOOL_COMMON_H */
