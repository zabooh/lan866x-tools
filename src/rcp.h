/*
 * rcp.h  -  Minimal C wrapper for the LAN866x Remote Control Protocol (RCP)
 *           over the pure-C SOME/IP stack (libsomeip).
 *
 * Goal: Windows prototype -> later ported 1:1 to a 32-bit MCU (lwIP + FreeRTOS).
 * Only the GPIO/I2C/SPI methods for the control-endpoint use case.
 *
 * Service: LAN866x Endpoint Service, service ID 0xFF10.
 * Method IDs verified against the authoritative Microchip SOME/IP dissector
 * table (Wireshark: SOMEIP_method_event_identifiers). SD runs on UDP 30490,
 * method endpoints on UDP 6800.
 *
 * ============================================================================
 * RECOMMENDED EMBEDDED BASE (official C client):
 *   Microchip ships a generated, callback-based pure-C client (`lan866x_c/`,
 *   lan866x_client.c/.h + lan866x_common.h) in the multi-language library repo
 *   (see the "LAN866x Library Integration Manual"). That client is the ideal
 *   drop-in for the MCU32 port: same RT_* return codes and the same *Var_t /
 *   *Reply_t structs used here (lan866x_common.h). This rcp.c/.h is a compact,
 *   self-contained stand-in on top of `libsomeip` for when the generated
 *   client is not vendored. See PORTING.md.
 *
 * STATUS: SCAFFOLD. The payload encodings tagged [V3]/[V4] and the RX dispatch
 *   in on_data_received() (rcp.c) still need to be completed from the official
 *   client. Method IDs themselves are verified.
 * ============================================================================
 */
#ifndef RCP_H
#define RCP_H

#include <stdint.h>
#include <stdbool.h>

/* --- Service ID of the LAN866x Endpoint Service --- */
#define RCP_SERVICE_ID            0xFF10u

/* --- Method IDs (source: SOMEIP_method_event_identifiers dissector table) - */
/* General / system */
#define RCP_M_REBOOT              0x1000u
#define RCP_M_IDENTIFY            0x1001u
#define RCP_M_GET_STATUS          0x1002u
#define RCP_M_READ_DIAGNOSIS_DATA 0x1003u
#define RCP_M_START_UPDATE        0x1004u
#define RCP_M_WRITE_IMAGE         0x1005u
#define RCP_M_FINISH_UPDATE       0x1006u
#define RCP_M_GET_WALLCLOCK       0x1007u
#define RCP_M_LOCK                0x1008u
#define RCP_M_SHUTDOWN            0x1009u
#define RCP_M_GET_NETWORK_STATUS  0x1600u
#define RCP_M_WAKEUP_NETWORK      0x1601u
#define RCP_M_CONFIG_DIGITAL_PIN  0x1100u
#define RCP_M_RELEASE_DIGITAL_PINS 0x1105u
/* GPIO */
#define RCP_M_OPEN_GPIO           0x1300u
#define RCP_M_CLOSE_GPIO          0x1302u
#define RCP_M_SET_GPIO            0x1330u
#define RCP_M_GET_GPIO            0x1332u
#define RCP_M_SET_AND_GET_GPIO    0x1334u
/* I2C */
#define RCP_M_OPEN_I2C            0x1200u
#define RCP_M_CLOSE_I2C           0x1202u
#define RCP_M_WRITE_I2C           0x1204u
#define RCP_M_READ_I2C            0x1220u
#define RCP_M_WRITE_AND_READ_I2C  0x1208u
/* SPI */
#define RCP_M_OPEN_SPI            0x1500u
#define RCP_M_CLOSE_SPI           0x1502u
#define RCP_M_WRITE_AND_READ_SPI  0x1508u
/* UART */
#define RCP_M_OPEN_UART           0x1400u
#define RCP_M_WRITE_UART          0x1404u
#define RCP_M_READ_UART           0x1420u
/* ADC */
#define RCP_M_OPEN_ADC            0x1700u
#define RCP_M_CLOSE_ADC           0x1702u
#define RCP_M_READ_ADC            0x1720u
/* PWM */
#define RCP_M_OPEN_PWM            0x1800u
#define RCP_M_CLOSE_PWM           0x1802u
#define RCP_M_WRITE_PWM           0x1804u
/* Events (server -> host notifications) */
#define RCP_E_ON_GPIO_EVENTS      0x8000u
#define RCP_E_ON_UART_RECEIVE     0x8010u
#define RCP_E_ON_TD_COMPLETED     0x8020u

/* --- Example pin mapping LAN8660 ---------- */
/* Pin IDs PA00..PA15 = 0..15 (order per Configurable Digital I/O Pins). */
#define PIN_PA00  0u
#define PIN_PA01  1u
#define PIN_PA02  2u   /* GPIO out */
#define PIN_PA03  3u   /* UART RX (SER0) */
#define PIN_PA04  4u   /* I2C SDA (SER1) */
#define PIN_PA05  5u   /* I2C SCL (SER1) */
#define PIN_PA06  6u   /* GPIO out */
#define PIN_PA07  7u
#define PIN_PA08  8u   /* SPI SDI/POCI (SER2) */
#define PIN_PA09  9u   /* SPI SCK  (SER2) */
#define PIN_PA10 10u   /* SPI CS_N (SER2) */
#define PIN_PA11 11u   /* SPI SDO/PICO (SER2) */

/* --- Endpoint discovery -------------------------------------------------- */
/* One entry per reachable endpoint (from EV_CLIENT_SERVICE_AVAILABLE). */
#define RCP_MAX_ENDPOINTS  16u
typedef struct {
    uint8_t  ip[4];        /* endpoint source IP (pIp->sourceAddr) */
    uint16_t port;         /* SOME/IP port (pIp->port)             */
    uint16_t serviceId;    /* offered service (0xFF10 = RCP)       */
    uint16_t instanceId;   /* receivedInstanceId                   */
    bool     available;    /* currently available?                 */
} rcp_endpoint_t;

/* Returns the currently discovered endpoints; return value = count. */
uint8_t rcp_get_endpoints(rcp_endpoint_t *out, uint8_t maxOut);

/* Active target endpoint for method calls (index in the list). */
bool rcp_select_endpoint(uint8_t index);

/* --- Lifecycle ---------------------------------------------------------- */
/* Initializes the SOME/IP stack + UDP transport and subscribes to service 0xFF10.
 * localIfIP: IP of the T1S-USB interface (e.g. {192,168,0,100}).          */
bool rcp_init(const uint8_t localIfIP[4]);

/* Must be called periodically: process RX + tick timers.
 * (Windows: from main loop; MCU32: from the SOME/IP task.)                */
void rcp_poll(void);

/* true once at least one endpoint service has been discovered.            */
bool rcp_is_ready(void);

/* --- GPIO --------------------------------------------------------------- */
bool rcp_open_gpio(const uint8_t *pinIds, uint8_t count);
bool rcp_set_gpio(const uint8_t *gpioValues, uint8_t len);  /* bitmask/values per pin */
bool rcp_get_gpio(uint8_t *outValues, uint8_t *outLen);

/* --- I2C ---------------------------------------------------------------- */
bool rcp_open_i2c(uint8_t pinSda, uint8_t pinScl, uint32_t clockHz);
bool rcp_write_read_i2c(uint8_t devAddr,
                        const uint8_t *tx, uint16_t txLen,
                        uint8_t *rx, uint16_t rxLen);

/* --- SPI ---------------------------------------------------------------- */
bool rcp_open_spi(uint8_t pinMiso, uint8_t pinSck, uint8_t pinCs, uint8_t pinMosi,
                  uint8_t mode, uint32_t clockHz);
bool rcp_write_read_spi(const uint8_t *tx, uint16_t txLen, uint8_t *rx, uint16_t rxLen);

/* --- ADC ---------------------------------------------------------------- */
/* channel: 0 = analog input, 1 = internal temperature.
 * vref:    0 = 3V3 (VDDA33),  1 = 1V1.   out = 12-bit raw value (0..4095).  */
bool rcp_open_adc(void);
bool rcp_read_adc(uint8_t channel, uint8_t vref, uint16_t *out);

/* --- PWM ---------------------------------------------------------------- */
/* intervalNs = period in ns; dutyQ31 = duty cycle (0 = 0% .. 2^31 = 100%).  */
bool rcp_open_pwm(uint8_t pin, uint32_t intervalNs, uint32_t dutyQ31);
bool rcp_write_pwm(uint32_t dutyQ31);

#endif /* RCP_H */
