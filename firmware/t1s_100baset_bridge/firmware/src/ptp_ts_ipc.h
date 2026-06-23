/*
 * ptp_ts_ipc.h — PTP RX-Timestamp Inter-Process Communication
 *
 * The LAN865x TC6 driver extracts the hardware RX-timestamp from the SPI footer
 * and stores it here via TC6_CB_OnRxEthernetPacket() in drv_lan865x_api.c.
 * The application reads it in pktEth0Handler() (app.c) when a PTP frame arrives.
 *
 * Timestamp format:  Bit[63:32] = seconds low 32-bit
 *                    Bit[31: 0] = nanoseconds
 */

#ifndef PTP_TS_IPC_H
#define PTP_TS_IPC_H

#include <stdint.h>
#include <stdbool.h>

/* Must match the definition in drv_lan865x_api.c */
typedef struct { uint64_t rxTimestamp; bool valid; } PTP_RxTimestampEntry_t;

/* Defined in drv_lan865x_api.c, written by the TC6 callback, read by pktEth0Handler */
extern volatile PTP_RxTimestampEntry_t g_ptp_rx_ts;

#endif /* PTP_TS_IPC_H */
