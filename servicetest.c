/*
 * servicetest.c  -  Probe which RCP methods an endpoint implements. Pure C.
 *
 * For every known RCP method it sends the method with an EMPTY payload and looks
 * at the SOME/IP return code:
 *   RT_UNKNOWN_METHOD (0x03)  -> the method is NOT implemented in this build.
 *   anything else             -> the handler exists (it usually answers
 *                                RT_MALFORMED_MESSAGE 0x09 because the params were
 *                                empty - which still proves it is present).
 *
 * This is SAFE: the firmware looks up the method ID first and, for an implemented
 * method, rejects the empty/malformed payload before executing anything. The few
 * genuinely destructive methods (Reboot / firmware update) are skipped by default;
 * pass --unsafe to probe them too (still empty-payload, so they only get as far as
 * a malformed-parameter rejection).
 *
 * Usage:
 *   lan866x-servicetest                    probe the safe method set
 *   lan866x-servicetest --ip 192.168.0.50
 *   lan866x-servicetest --unsafe           also probe Reboot/Update methods
 */
#include <stdlib.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

typedef struct { uint16_t id; const char *name; const char *group; int unsafe; } method_t;

static const method_t METHODS[] = {
    { 0x1002, "GetStatus",          "System",            0 },
    { 0x1600, "GetNetworkStatus",   "System",            0 },
    { 0x1003, "ReadDiagnosisData",  "System",            0 },
    { 0x1601, "WakeupNetwork",      "System",            0 },
    { 0x1000, "Reboot",             "Bootloader/Update", 1 },
    { 0x1004, "StartUpdate",        "Bootloader/Update", 1 },
    { 0x1005, "WriteImage",         "Bootloader/Update", 1 },
    { 0x1006, "FinishUpdate",       "Bootloader/Update", 1 },
    { 0x1105, "ReleaseDigitalPins", "Digital pins",      0 },
    { 0x1300, "OpenGpio",           "GPIO",              0 },
    { 0x1330, "SetGpio",            "GPIO",              0 },
    { 0x1332, "GetGpio",            "GPIO",              0 },
    { 0x1200, "OpenI2C",            "I2C",               0 },
    { 0x1204, "WriteI2C",           "I2C",               0 },
    { 0x1220, "ReadI2C",            "I2C",               0 },
    { 0x1208, "WriteAndReadI2C",    "I2C",               0 },
    { 0x1202, "CloseI2C",           "I2C",               0 },
    { 0x1500, "OpenSpi",            "SPI",               0 },
    { 0x1508, "WriteAndReadSpi",    "SPI",               0 },
    { 0x1509, "WriteAndReadSpi2",   "SPI",               0 },
    { 0x1502, "CloseSpi",           "SPI",               0 },
    { 0x1400, "OpenUart",           "UART",              0 },
    { 0x1404, "WriteUart",          "UART",              0 },
    { 0x1420, "ReadUart",           "UART",              0 },
    { 0x1700, "OpenAdc",            "ADC",               0 },
    { 0x1720, "ReadAdc",            "ADC",               0 },
    { 0x1702, "CloseAdc",           "ADC",               0 },
    { 0x1800, "OpenPwm",            "PWM",               0 },
    { 0x1804, "WritePwm",           "PWM",               0 },
    { 0x1802, "ClosePwm",           "PWM",               0 },
};

static volatile int      g_done = 0;
static volatile ReturnCode_t g_rc = RT_TIMEOUT;
static void on_probe(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{ (void)ctx; (void)rx; (void)rxLen; g_rc = rc; g_done = 1; }

/* Send one method with empty params; return its return code (RT_TIMEOUT if no
 * answer after retries). */
static ReturnCode_t probe(uint16_t method)
{
    int attempt;
    for (attempt = 0; attempt < 3; ++attempt) {
        g_done = 0; g_rc = RT_TIMEOUT;
        if (rcp_async_request(method, NULL, 0, on_probe, NULL) != RT_OK) { Sleep(30); continue; }
        while (!g_done) { rcp_async_poll(); Sleep(2); }
        if (g_rc != RT_TIMEOUT) return g_rc;
        Sleep(20);
    }
    return RT_TIMEOUT;
}

static const char *verdict(ReturnCode_t rc)
{
    switch (rc) {
        case RT_UNKNOWN_METHOD: return "ABSENT";
        case RT_TIMEOUT:        return "no response";
        case RT_NOT_READY:      return "present (not ready)";
        default:                return "PRESENT";
    }
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, unsafe = 0;
    int nPresent = 0, nAbsent = 0, nOther = 0, nSkipped = 0;
    const char *lastGroup = "";

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-servicetest - probe which RCP methods an endpoint implements (pure C)\n"
                   "  --unsafe     also probe Reboot/Update methods (still empty-payload only)\n"
                   "  --ip/--ep    target endpoint\n"
                   "A method that answers RT_UNKNOWN_METHOD (0x03) is not implemented; any other\n"
                   "return code means the handler exists (empty params -> usually 0x09 malformed).\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip") && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")   && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--unsafe")) unsafe = 1;
    }

    if (tool_select(wantIp, wantEp, 5, "LAN866x RCP service probe (pure C)") < 0) return 2;

    rcp_set_async_timeout_ms(400);

    printf("\nProbing %d known RCP methods (empty payload). 0x03=absent, else present.\n",
           (int)(sizeof(METHODS)/sizeof(METHODS[0])));
    if (!unsafe) printf("(Reboot/Update methods skipped - pass --unsafe to include them.)\n");

    for (i = 0; i < (int)(sizeof(METHODS)/sizeof(METHODS[0])); ++i) {
        const method_t *m = &METHODS[i];
        ReturnCode_t rc;
        if (strcmp(m->group, lastGroup) != 0) { printf("\n  [%s]\n", m->group); lastGroup = m->group; }

        if (m->unsafe && !unsafe) {
            printf("    %-20s 0x%04X  -  skipped (unsafe; use --unsafe)\n", m->name, m->id);
            nSkipped++;
            continue;
        }

        rc = probe(m->id);
        printf("    %-20s 0x%04X  rc=0x%04X  %s\n", m->name, m->id, (unsigned)rc, verdict(rc));
        if (rc == RT_UNKNOWN_METHOD)   nAbsent++;
        else if (rc == RT_TIMEOUT)     nOther++;
        else                           nPresent++;
        Sleep(20);   /* pace control traffic (gotcha #4) */
    }

    printf("\nSummary: %d present, %d absent, %d no-response, %d skipped.\n",
           nPresent, nAbsent, nOther, nSkipped);
    return 0;
}
