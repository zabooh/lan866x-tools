/*
 * lan866x_cli.h - Bridge CLI integration of the LAN866x SOME/IP client.
 *
 * Mirrors the lan866x-* host tools as Harmony SYS_CMD commands on the bridge.
 * The bridge becomes a SOME/IP client on the T1S side and talks RCP to the
 * endpoint. See plat_h3tcpip.c (platform layer) and PORTING.md.
 */
#ifndef LAN866X_CLI_H
#define LAN866X_CLI_H

/* Register the "lan866x" SYS_CMD command group. Call once from APP_Initialize. */
void LAN866X_CLI_Init(void);

/* Drive the SOME/IP client: lazily runs rcp_init() once an interface is up,
 * then pumps rcp_poll() each call. Call once per superloop iteration from
 * APP_Tasks (cooperative single-thread - do NOT block the superloop). */
void LAN866X_CLI_Task(void);

/* clickdemo (clickdemo_cli.c): drive the two RGB displays from the Thumbstick
 * (SPI) + Proximity (I2C) for `seconds`, streaming RTP video to the endpoint.
 * Bounded so it never freezes the bridge superloop. */
void clickdemo_run(uint32_t seconds, int fps, int bright, int proxMax, int barBlue);

#endif /* LAN866X_CLI_H */
