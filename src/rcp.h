/*
 * rcp.h  -  C wrapper for the LAN866x Remote Control Protocol (RCP) over the
 *           pure-C SOME/IP stack (libsomeip). No C++.
 *
 * Goal: Windows host today, ported 1:1 to a 32-bit MCU (lwIP + FreeRTOS) by
 * swapping only the platform stub (someip_stub_win.c -> lwIP/FreeRTOS).
 *
 * The request/reply structs are the official ones from lan866x_common.h, and
 * every method is encoded/decoded exactly like the C++ client
 * (LAN866XClientImpl). Method IDs verified against the SOME/IP dissector table.
 *
 * Service: LAN866x Endpoint Service, service ID 0xFF10. SD on UDP 30490,
 * method endpoints on UDP 6800.
 */
#ifndef RCP_H
#define RCP_H

#include <stdint.h>
#include <stdbool.h>
#include "lan866x_common.h"   /* ReturnCode_t + all *Var_t / *Reply_t structs */

#define RCP_SERVICE_ID  0xFF10u

/* --- Endpoint discovery -------------------------------------------------- */
#define RCP_MAX_ENDPOINTS  16u
typedef struct {
    uint8_t  ip[4];        /* endpoint IP (from the SD OfferService)  */
    uint16_t port;         /* SOME/IP method port (6800)              */
    uint16_t serviceId;    /* 0xFF10 = RCP                            */
    uint16_t instanceId;   /* received instance id                    */
    bool     available;    /* currently available?                    */
} rcp_endpoint_t;

uint8_t rcp_get_endpoints(rcp_endpoint_t *out, uint8_t maxOut);
bool    rcp_select_endpoint(uint8_t index);

/* --- Lifecycle ----------------------------------------------------------- */
/* Init SOME/IP + UDP transport and request service 0xFF10. localIfIP may be
 * NULL (the platform stub picks the interface by subnet match). */
bool rcp_init(const uint8_t localIfIP[4]);
/* Service the transmit layer; call periodically while waiting. */
void rcp_poll(void);
/* true once at least one endpoint has offered the service. */
bool rcp_is_ready(void);
/* Per-request response timeout in ms (default 2000). Lower it for fast bus
 * probing (e.g. I2C address scan, where absent devices never reply). */
void rcp_set_timeout_ms(uint32_t ms);

/* --- Methods (signatures mirror the C++ LAN866XClient) ------------------- *
 * All return ReturnCode_t (RT_OK on success; RT_NOT_REACHABLE = peripheral
 * not configured on that node; RT_TIMEOUT = no response).                   */

/* System / diagnostics */
ReturnCode_t rcp_get_status(GetStatusReply_t *out);
ReturnCode_t rcp_get_network_status(GetNetworkStatusReply_t *out);

/* Digital pins */
ReturnCode_t rcp_release_digital_pins(const ReleaseDigitalPinsVar_t *in);

/* GPIO */
ReturnCode_t rcp_open_gpio(const OpenGpioVar_t *in, OpenGpioReply_t *out);
ReturnCode_t rcp_set_gpio(const SetGpioVar_t *in);
ReturnCode_t rcp_get_gpio(GetGpioReply_t *out);

/* I2C */
ReturnCode_t rcp_open_i2c(const OpenI2CVar_t *in, OpenI2CReply_t *out);
ReturnCode_t rcp_write_and_read_i2c(const WriteAndReadI2CVar_t *in, ReadI2CReply_t *out);
ReturnCode_t rcp_close_i2c(const CloseI2CVar_t *in);

/* SPI */
ReturnCode_t rcp_open_spi(const OpenSpiVar_t *in, OpenSpiReply_t *out);
ReturnCode_t rcp_write_and_read_spi(const WriteAndReadSpiVar_t *in, WriteAndReadSpiReply_t *out);
ReturnCode_t rcp_close_spi(const CloseSpiVar_t *in);

/* ADC */
ReturnCode_t rcp_open_adc(const OpenAdcVar_t *in, OpenAdcReply_t *out);
ReturnCode_t rcp_read_adc(const ReadAdcVar_t *in, ReadAdcReply_t *out);
ReturnCode_t rcp_close_adc(const CloseAdcVar_t *in);

/* PWM */
ReturnCode_t rcp_open_pwm(const OpenPwmVar_t *in, OpenPwmReply_t *out);
ReturnCode_t rcp_close_pwm(const ClosePwmVar_t *in);

/* --- Example pin mapping LAN8660 (PA00..PA15 = 0..15) -------------------- */
#define PIN_PA00  0u
#define PIN_PA02  2u   /* GPIO out */
#define PIN_PA03  3u   /* UART RX  */
#define PIN_PA04  4u   /* I2C SDA  */
#define PIN_PA05  5u   /* I2C SCL  */
#define PIN_PA06  6u   /* GPIO out / PWM */
#define PIN_PA08  8u   /* SPI / I2C */
#define PIN_PA09  9u

#endif /* RCP_H */
