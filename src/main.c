/*
 * main.c  -  Minimaler SOME/IP-Host (C) fuer LAN866x Control-Endpoints.
 *
 * Windows-Konsolen-Prototyp -> Vorlage fuer 32-bit Embedded (MCU32 + lwIP + FreeRTOS).
 * Use-Case: nur GPIO / I2C / SPI ueber einen LAN8660 Control-EP,
 * angebunden ueber den T1S-USB-Adapter (EVB-LAN8670-USB) als Ethernet-Bridge.
 *
 * Ablauf:
 *   1. rcp_init()  -> SOME/IP-Stack + UDP an das T1S-Interface binden
 *   2. warten bis rcp_is_ready()  (Service Discovery von 0xFF10)
 *   3. Peripherie oeffnen (OpenGpio/OpenI2C/OpenSpi)
 *   4. Demo-Loop: GPIO toggeln, I2C lesen, SPI-Transfer
 *
 * Plattform-Trennung (fuer den MCU32-Port):
 *   - main()-Loop  -> wird zur SOME/IP-Task (FreeRTOS)
 *   - rcp_poll()   -> bleibt gleich
 *   - UDP/Timer    -> in libsomeip/stub austauschen (siehe PORTING.md)
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

/* IP des T1S-USB-Interfaces auf dem PC (statisch im Endpoint-Subnetz setzen!). */
static const uint8_t LOCAL_IF_IP[4] = { 192, 168, 0, 100 };

int main(void)
{
    printf("LAN866x RCP C-Host Prototyp ueber SOME/IP\n");

    if (!rcp_init(LOCAL_IF_IP)) {
        fprintf(stderr, "rcp_init fehlgeschlagen (Interface/IP/Firewall pruefen)\n");
        return 1;
    }

    /* --- Auf Service Discovery warten (max. 5 s) --- */
    printf("Warte auf Endpoint-Service 0xFF10 ...\n");
    for (int i = 0; i < 500 && !rcp_is_ready(); ++i) { rcp_poll(); SLEEP_MS(10); }
    if (!rcp_is_ready()) { fprintf(stderr, "Kein Endpoint gefunden.\n"); return 2; }
    printf("Endpoint verfuegbar.\n");

    /* --- Peripherie konfigurieren (Beispiel-Pinbelegung) --- */
    const uint8_t gpioPins[] = { PIN_PA02, PIN_PA06 };
    rcp_open_gpio(gpioPins, (uint8_t)sizeof(gpioPins));
    rcp_open_i2c(PIN_PA04, PIN_PA05, 400000u);                       /* 400 kHz */
    rcp_open_spi(PIN_PA08, PIN_PA09, PIN_PA10, PIN_PA11, 0u, 1000000u);/* Mode0, 1 MHz */

    /* --- Demo-Loop --- */
    uint8_t led = 0;
    for (int loop = 0; loop < 50; ++loop) {

        /* GPIO toggeln (PA02/PA06) */
        led ^= 1u;
        uint8_t vals[2] = { led, (uint8_t)(led ^ 1u) };
        rcp_set_gpio(vals, sizeof(vals));

        /* I2C: 1 Byte vom Geraet 0x48 lesen (Beispiel-Sensor) */
        uint8_t reg = 0x00, rx[2] = {0};
        if (rcp_write_read_i2c(0x48u, &reg, 1u, rx, sizeof(rx)))
            printf("  I2C[0x48] reg0 = %02X %02X\n", rx[0], rx[1]);

        /* SPI: 3-Byte-Transfer (Beispiel) */
        uint8_t stx[3] = { 0x9F, 0x00, 0x00 }, srx[3] = {0};
        if (rcp_write_read_spi(stx, sizeof(stx), srx, sizeof(srx)))
            printf("  SPI rx = %02X %02X %02X\n", srx[0], srx[1], srx[2]);

        /* SOME/IP bedienen + Pause */
        for (int p = 0; p < 20; ++p) { rcp_poll(); SLEEP_MS(10); }
    }

    printf("Fertig.\n");
    return 0;
}
