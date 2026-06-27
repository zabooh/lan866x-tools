/*
 * lan866x_cli.h - Bridge CLI integration of the LAN866x SOME/IP client.
 *
 * Mirrors the lan866x-* host tools as Harmony SYS_CMD commands on the bridge.
 * The bridge becomes a SOME/IP client on the T1S side and talks RCP to the
 * endpoint. See plat_h3tcpip.c (platform layer) and PORTING.md.
 */
#ifndef LAN866X_CLI_H
#define LAN866X_CLI_H

#include <stdbool.h>
#include <stdint.h>

/* Register the "lan866x" SYS_CMD command group. Call once from APP_Initialize. */
void LAN866X_CLI_Init(void);

/* Shared helpers for the peripheral command files (gpio_cli/i2c_cli/spi_cli/...).
 * Select the first discovered endpoint as the RCP target; false if none yet. */
bool sel_first_ep(void);
/* Blocking SetGpio on an open GPIO handle (value 0/1); true on RT_OK. */
bool led_set(uint16_t handle, int value);

/* Register the additional peripheral/system/DNCP command groups. Each is called
 * once from APP_Initialize, alongside LAN866X_CLI_Init(). */
void GPIO_CLI_Init(void);   /* gpio, gpioevents, ledtoggle, ledpwm */
void I2C_CLI_Init(void);    /* i2cscan, i2cid, proxmon, lan8680, proxled */
void SPI_CLI_Init(void);    /* spi, spiid, thumbmon, adc, pwm */
void SYS_CLI_Init(void);    /* servicetest, boot, uart, video */
void DNCP_CLI_Init(void);   /* dncpmon, dncpdisc */
void HWCLK_Init(void);      /* hwclk rev/xosc... - hardware time-base bring-up (hwclk_cli.c) */

/* NTP software time sync (ntp_sync.c): a free-running high-res counter the PC
 * disciplines to its wall clock via a UDP t1/t2/t3/t4 exchange (port 30491). */
void     NTP_Init(void);    /* open the UDP service + register the "ntp" CLI group */
void     NTP_Task(void);    /* service the UDP socket; call once per superloop tick */
uint64_t ntp_now_ns(void);  /* disciplined NTP time in ns (PC-aligned once synced) */
void     ntp_tap_eth0(uint8_t dir, const uint8_t *frame, uint16_t len);  /* eth0 timestamp tap (0=RX,1=TX) */

/* Drive the SOME/IP client: lazily runs rcp_init() once an interface is up,
 * then pumps rcp_poll() each call. Call once per superloop iteration from
 * APP_Tasks (cooperative single-thread - do NOT block the superloop). */
void LAN866X_CLI_Task(void);

/* clickdemo (clickdemo_cli.c): drive the two RGB displays from the Thumbstick
 * (SPI) + Proximity (I2C) for `seconds`, streaming RTP video to the endpoint.
 * Bounded so it never freezes the bridge superloop. */
void clickdemo_run(uint32_t seconds, int fps, int bright, int proxMax, int barBlue);

#endif /* LAN866X_CLI_H */
