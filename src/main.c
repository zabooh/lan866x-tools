/*
 * main.c  -  Minimal SOME/IP host (C) for LAN866x control endpoints.
 *
 * Windows console prototype -> template for 32-bit embedded (MCU32 + lwIP + FreeRTOS).
 * Use case: only GPIO / I2C / SPI via a LAN8660 control EP, connected through
 * the T1S-USB adapter (EVB-LAN8670-USB) as an Ethernet bridge.
 *
 * Flow:
 *   1. rcp_init()  -> bind SOME/IP stack + UDP to the T1S interface
 *   2. wait until rcp_is_ready()  (service discovery of 0xFF10)
 *   3. open peripherals (OpenGpio/OpenI2C/OpenSpi)
 *   4. demo loop: toggle GPIO, read I2C, SPI transfer
 *
 * Platform separation (for the MCU32 port):
 *   - main() loop  -> becomes the SOME/IP task (FreeRTOS)
 *   - rcp_poll()   -> stays the same
 *   - UDP/timer    -> replace in libsomeip/stub (see PORTING.md)
 */
#include <stdio.h>
#include <string.h>
#include "rcp.h"

#ifdef _WIN32
#  include <windows.h>
#  define SLEEP_MS(ms) Sleep(ms)
#else
#  include <time.h>
static void SLEEP_MS(unsigned ms){ struct timespec t={ms/1000,(long)(ms%1000)*1000000L}; nanosleep(&t,0); }
#endif

/* IP of the T1S-USB interface on the PC (set statically in the endpoint subnet!). */
static const uint8_t LOCAL_IF_IP[4] = { 192, 168, 0, 100 };

int main(void)
{
    printf("LAN866x RCP C host prototype over SOME/IP\n");

    if (!rcp_init(LOCAL_IF_IP)) {
        fprintf(stderr, "rcp_init failed (check interface/IP/firewall)\n");
        return 1;
    }

    /* --- wait for service discovery (max 5 s) --- */
    printf("Waiting for endpoint service 0xFF10 ...\n");
    for (int i = 0; i < 500 && !rcp_is_ready(); ++i) { rcp_poll(); SLEEP_MS(10); }
    if (!rcp_is_ready()) { fprintf(stderr, "No endpoint found.\n"); return 2; }
    printf("Endpoint available.\n");

    /* --- configure peripherals (example pin mapping) --- */
    const uint8_t gpioPins[] = { PIN_PA02, PIN_PA06 };
    rcp_open_gpio(gpioPins, (uint8_t)sizeof(gpioPins));
    rcp_open_i2c(PIN_PA04, PIN_PA05, 400000u);                       /* 400 kHz */
    rcp_open_spi(PIN_PA08, PIN_PA09, PIN_PA10, PIN_PA11, 0u, 1000000u);/* mode 0, 1 MHz */

    /* --- demo loop --- */
    uint8_t led = 0;
    for (int loop = 0; loop < 50; ++loop) {

        /* toggle GPIO (PA02/PA06) */
        led ^= 1u;
        uint8_t vals[2] = { led, (uint8_t)(led ^ 1u) };
        rcp_set_gpio(vals, sizeof(vals));

        /* I2C: read 1 byte from device 0x48 (example sensor) */
        uint8_t reg = 0x00, rx[2] = {0};
        if (rcp_write_read_i2c(0x48u, &reg, 1u, rx, sizeof(rx)))
            printf("  I2C[0x48] reg0 = %02X %02X\n", rx[0], rx[1]);

        /* SPI: 3-byte transfer (example) */
        uint8_t stx[3] = { 0x9F, 0x00, 0x00 }, srx[3] = {0};
        if (rcp_write_read_spi(stx, sizeof(stx), srx, sizeof(srx)))
            printf("  SPI rx = %02X %02X %02X\n", srx[0], srx[1], srx[2]);

        /* service SOME/IP + pause */
        for (int p = 0; p < 20; ++p) { rcp_poll(); SLEEP_MS(10); }
    }

    printf("Done.\n");
    return 0;
}
