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
/* Per-request response timeout in ms (default 1500). Lower it for fast bus
 * probing (e.g. I2C address scan, where absent devices never reply). */
void rcp_set_timeout_ms(uint32_t ms);
/* Extra attempts on RT_TIMEOUT (default 3). Set 0 for fast probing where a
 * non-answer is the expected/normal result (e.g. I2C address scan). */
void rcp_set_retries(uint8_t n);
/* WriteImage chunk size in bytes (default 1200 = max). Large frames see more
 * loss on a marginal T1S link, but the WriteId-idempotent retries cover it. */
void rcp_set_chunk(uint16_t n);

/* --- Methods (signatures mirror the C++ LAN866XClient) ------------------- *
 * All return ReturnCode_t (RT_OK on success; RT_NOT_REACHABLE = peripheral
 * not configured on that node; RT_TIMEOUT = no response).                   */

/* System / diagnostics */
ReturnCode_t rcp_get_status(GetStatusReply_t *out);
ReturnCode_t rcp_get_network_status(GetNetworkStatusReply_t *out);
/* PHY/T1S diagnosis data (SQI, fault/short detection, ...): 4 raw 16-byte
 * channel blobs straight from the endpoint. Method 0x1003. */
ReturnCode_t rcp_read_diagnosis_data(ReadDiagnosisDataReply_t *out);

/* --- Bootloader / reboot ------------------------------------------------ *
 * Reboot the endpoint into the named image. The name is sent BOM-prefixed
 * (UTF-8 BOM + name + NUL), exactly like the official flasher. The node then
 * leaves and re-joins the bus (re-discovery via the SD event callback).     */
#define RCP_IMAGE_BOOTLOADER  "bootloader/app.bin"
#define RCP_IMAGE_MAIN        "main/app.bin"
ReturnCode_t rcp_reboot(const char *imageName);

/* --- Firmware/config update (run while the endpoint is in the bootloader) -
 * Each image (e.g. "main/config.bin", "main/app.bin") is written as:
 *   StartUpdate(name, IV) -> WriteImage(name, writeId++, chunk)... -> FinishUpdate(name, signature)
 * The blobs are the pre-built, signed/encrypted parts from an MCHPKG; the host
 * only transports them - the bootloader verifies the signature. WriteId makes
 * a resent chunk idempotent (a lost ack is harmless).                        */
ReturnCode_t rcp_start_update(const StartUpdateVar_t *in);
ReturnCode_t rcp_write_image(const WriteImageVar_t *in, WriteImageReply_t *out);
ReturnCode_t rcp_finish_update(const FinishUpdateVar_t *in);

typedef void (*rcp_progress_cb)(uint32_t done, uint32_t total);
/* High-level: flash one image. imageName is the logical name (BOM added
 * internally), e.g. "main/config.bin". */
ReturnCode_t rcp_flash_image(const char *imageName,
                             const uint8_t *data, uint32_t dataLen,
                             const uint8_t *iv,   uint16_t ivLen,
                             const uint8_t *sig,  uint16_t sigLen,
                             rcp_progress_cb cb);

/* Digital pins */
ReturnCode_t rcp_release_digital_pins(const ReleaseDigitalPinsVar_t *in);

/* GPIO */
ReturnCode_t rcp_open_gpio(const OpenGpioVar_t *in, OpenGpioReply_t *out);
ReturnCode_t rcp_set_gpio(const SetGpioVar_t *in);
ReturnCode_t rcp_get_gpio(GetGpioReply_t *out);

/* I2C */
ReturnCode_t rcp_open_i2c(const OpenI2CVar_t *in, OpenI2CReply_t *out);
ReturnCode_t rcp_write_i2c(const WriteI2CVar_t *in);
ReturnCode_t rcp_write_and_read_i2c(const WriteAndReadI2CVar_t *in, ReadI2CReply_t *out);
ReturnCode_t rcp_close_i2c(const CloseI2CVar_t *in);

/* SPI */
ReturnCode_t rcp_open_spi(const OpenSpiVar_t *in, OpenSpiReply_t *out);
ReturnCode_t rcp_write_and_read_spi(const WriteAndReadSpiVar_t *in, WriteAndReadSpiReply_t *out);
/* Compound transfer: two SPI elements in ONE round-trip (method 0x1509,
 * config >= V1.3.2). Halves latency for paired reads like a 2-axis ADC.
 * rd*Len are in/out (capacity in, bytes read out). Returns RT_UNKNOWN_METHOD
 * if the endpoint's config is too old. */
ReturnCode_t rcp_write_and_read_spi2(uint16_t handle, uint32_t writeId,
                                     const uint8_t *cmd0, uint16_t cmd0Len, uint8_t *rd0, uint16_t *rd0Len,
                                     const uint8_t *cmd1, uint16_t cmd1Len, uint8_t *rd1, uint16_t *rd1Len);
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
