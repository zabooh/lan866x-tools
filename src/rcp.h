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

/* --- Asynchronous (non-blocking) requests -------------------------------- *
 * rcp_async_request() builds + sends a request and returns immediately. The
 * reply payload (or RT_TIMEOUT) is delivered later to cb - synchronously from
 * rcp_async_poll() (single-thread: it both pumps the RX and fires the deadline,
 * so the callback always runs on the caller's strand, never concurrently).
 * Call rcp_async_poll() periodically to drive completions and timeouts. Lets a
 * fixed-rate loop (e.g. a video stream) keep running while sensor reads are in
 * flight, instead of blocking on each round-trip. Up to RCP_ASYNC_MAX may be
 * outstanding. The callback runs inline on the polling strand - keep it short;
 * no volatile/atomic/lock is needed for the state it touches. */
#define RCP_ASYNC_MAX 16
typedef void (*rcp_async_cb)(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen);
ReturnCode_t rcp_async_request(uint16_t methodId, const uint8_t *params, uint16_t paramLen,
                               rcp_async_cb cb, void *ctx);
void rcp_async_poll(void);
void rcp_set_async_timeout_ms(uint32_t ms);   /* per-request deadline (default 150) */
/* SOME/IP session id of the most recent successful rcp_async_request(), to
 * correlate a request/reply with a capture (someip.sessionid). */
uint16_t rcp_async_last_sid(void);

/* Param builders / reply decoders to pair with rcp_async_request (the WTLV
 * encoding stays inside rcp). Return 0 on encode error; decoders return false
 * on a malformed reply. rd*Len are in/out (capacity in, bytes read out). */
uint16_t rcp_enc_spi2(uint8_t *buf, uint16_t cap, uint16_t handle, uint32_t writeId,
                      const uint8_t *c0, uint16_t c0len, const uint8_t *c1, uint16_t c1len,
                      uint16_t r0len, uint16_t r1len);
bool     rcp_dec_spi2(const uint8_t *rx, uint16_t rxLen, uint8_t *rd0, uint16_t *l0, uint8_t *rd1, uint16_t *l1);
uint16_t rcp_enc_i2c_read(uint8_t *buf, uint16_t cap, uint16_t handle, uint16_t addr, uint32_t writeId,
                          const uint8_t *wr, uint16_t wrlen, uint16_t rdlen);
bool     rcp_dec_i2c_read(const uint8_t *rx, uint16_t rxLen, uint8_t *rd, uint16_t *rdLen);
/* SetGpio (0x1330) params for the async path: one 3-byte tuple
 * [handleHi, handleLo, value] as BLOB tag 0 (same wire format as rcp_set_gpio). */
uint16_t rcp_enc_gpio_set(uint8_t *buf, uint16_t cap, uint16_t handle, uint8_t value);
/* WriteAndReadSpi (method 0x1508): one full-duplex transfer for the async path -
 * handle, readLen, writeId, tx blob. Decode the reply with rcp_dec_spi1. */
uint16_t rcp_enc_spi1(uint8_t *buf, uint16_t cap, uint16_t handle, uint32_t writeId,
                      const uint8_t *tx, uint16_t txLen, uint16_t rdLen);
bool     rcp_dec_spi1(const uint8_t *rx, uint16_t rxLen, uint8_t *rd, uint16_t *rdLen);
/* WritePwm (method 0x1804) params for the async path: handle, writeId, dutyQ31. */
uint16_t rcp_enc_pwm_write(uint8_t *buf, uint16_t cap, uint16_t handle, uint32_t writeId, uint32_t dutyQ31);

/* --- Methods (signatures mirror the C++ LAN866XClient) ------------------- *
 * All return ReturnCode_t (RT_OK on success; RT_NOT_REACHABLE = peripheral
 * not configured on that node; RT_TIMEOUT = no response).                   */

/* System / diagnostics */
ReturnCode_t rcp_get_status(GetStatusReply_t *out);
ReturnCode_t rcp_get_network_status(GetNetworkStatusReply_t *out);
/* PHY/T1S diagnosis data (SQI, fault/short detection, ...): 4 raw 16-byte
 * channel blobs straight from the endpoint. Method 0x1003. */
ReturnCode_t rcp_read_diagnosis_data(ReadDiagnosisDataReply_t *out);
/* Module health monitor: running app name, uptime (ns), health record. Method
 * 0x100A. Layout per v1.10.0 proto - unverified on V1.3.2/V1.4.0 firmware. */
ReturnCode_t rcp_get_health_status(GetHealthStatusReply_t *out);
/* Reset network diagnosis counters by category (0=all). Method 0x1605. */
ReturnCode_t rcp_clear_network_counters(const ClearNetworkCountersVar_t *in);
/* Start a topology/propagation-delay measurement (Role 0=initiator,1=reference,
 * 2=measurement). Method 0x1602. The result arrives via GetTDMeasurementResult
 * (0x1603) or the OnTDMeasurementCompleted event (0x8020). */
ReturnCode_t rcp_start_td_measurement(const StartTDMeasurementVar_t *in);

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
ReturnCode_t rcp_read_i2c(const ReadI2CVar_t *in, ReadI2CReply_t *out);  /* pure read, method 0x1220 */
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
/* Change the duty cycle of an open PWM (method 0x1804). DutyCycle: 0=0% .. 2^31=100%. */
ReturnCode_t rcp_write_pwm(const WritePwmVar_t *in);

/* UART (methods 0x1400/0x1404/0x1420). Open returns a handle; Write is a
 * BasicReply (no payload); Read pulls whatever is buffered on the endpoint.
 * For unsolicited RX, set OpenUartVar.Notification=1 and register an
 * rcp_uart_receive_cb (see events below) instead of polling rcp_read_uart. */
ReturnCode_t rcp_open_uart(const OpenUartVar_t *in, OpenUartReply_t *out);
ReturnCode_t rcp_write_uart(const WriteUartVar_t *in);
ReturnCode_t rcp_read_uart(const ReadUartVar_t *in, ReadUartReply_t *out);

/* --- Events / notifications (device -> host) ----------------------------- *
 * The endpoint can push GPIO edge events, UART RX and ADC threshold events as
 * SOME/IP notifications instead of being polled. They are NOT received by
 * default: call rcp_enable_event_subscription(true) BEFORE rcp_init() to join
 * eventgroup 0x2000, then register the callbacks you want. Decoded payloads are
 * delivered synchronously from rcp_poll()/rcp_async_poll() on the single strand
 * (keep callbacks short; do not re-enter rcp_*). ctx is passed back verbatim. */
void rcp_enable_event_subscription(bool on);
typedef void (*rcp_gpio_events_cb)(const OnGpioEventsNotification_t *ev, void *ctx);
typedef void (*rcp_uart_receive_cb)(const OnUartReceiveNotification_t *ev, void *ctx);
typedef void (*rcp_adc_event_cb)(const OnAdcEventNotification_t *ev, void *ctx);
void rcp_set_gpio_events_cb(rcp_gpio_events_cb cb, void *ctx);   /* event 0x8000 */
void rcp_set_uart_receive_cb(rcp_uart_receive_cb cb, void *ctx); /* event 0x8010 */
void rcp_set_adc_event_cb(rcp_adc_event_cb cb, void *ctx);       /* event 0x8030 */

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
