/*
 * gpio_cli.c - GPIO/LED family of the bridge CLI: mirrors the host tools
 *   gpio, gpioevents, ledtoggle, ledpwm  (see ../../.. /{gpio,gpioevents,
 *   ledtoggle,ledpwm}.c). Commands are registered as the "gpio" SYS_CMD group;
 *   type the name directly. Long-runners are bounded ([secs]) and abortable with
 *   Ctrl-C / 'q'; their loop pumps the TCP/IP stack + console via plat_sleep_ms
 *   so the bridge keeps running. No C++.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "definitions.h"
#include "system/command/sys_command.h"
#include "config/default/system/console/sys_console.h"
#include "rcp.h"
#include "plat.h"
#include "lan866x_cli.h"

/* Read the console RX ring (filled by SYS_CONSOLE_Tasks, pumped from
 * plat_sleep_ms while a handler blocks); return 1 on Ctrl-C (0x03) or 'q'. */
static int chk_abort(SYS_CONSOLE_HANDLE con)
{
    char ch;
    int hit = 0;
    while (SYS_CONSOLE_Read(con, &ch, 1) > 0)
        if (ch == 0x03 || ch == 'q' || ch == 'Q') hit = 1;
    return hit;
}

/* --- gpio <pin> <0|1|get> : set or read one GPIO (mirrors gpio.c) ---------- */
static void cmd_gpio(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    ReleaseDigitalPinsVar_t rel; OpenGpioVar_t ov; OpenGpioReply_t orep;
    uint8_t pin;
    int doGet, setVal = 0;
    (void)pCmdIO;

    if (argc < 3) {
        SYS_CONSOLE_PRINT("Usage: gpio <pin 0..15> <0|1|get>\r\n");
        return;
    }
    pin = (uint8_t)strtoul(argv[1], NULL, 10);
    doGet = (strcmp(argv[2], "get") == 0);
    if (!doGet) setVal = (strtoul(argv[2], NULL, 10) != 0u);
    if (pin > 15u) { SYS_CONSOLE_PRINT("pin must be 0..15\r\n"); return; }

    if (!sel_first_ep()) { SYS_CONSOLE_PRINT("[gpio] no endpoint - run 'discovery' first\r\n"); return; }
    rcp_set_timeout_ms(800); rcp_set_retries(2);

    memset(&rel, 0, sizeof(rel)); rel.PinIdList[0] = pin; rel.PinIdListLength = 1;
    rcp_release_digital_pins(&rel);
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdGpio = pin; ov.Direction = doGet ? 0 : 1;   /* input : output low */
    if (rcp_open_gpio(&ov, &orep) != RT_OK) { SYS_CONSOLE_PRINT("OpenGpio failed on PA%02u\r\n", (unsigned)pin); goto done; }

    if (doGet) {
        GetGpioReply_t gr; int v = -1, i;
        memset(&gr, 0, sizeof(gr));
        if (rcp_get_gpio(&gr) != RT_OK) { SYS_CONSOLE_PRINT("GetGpio failed\r\n"); goto done; }
        for (i = 0; i + 2 < (int)gr.GpioValuesLength; i += 3) {
            uint16_t h = (uint16_t)((gr.GpioValues[i] << 8) | gr.GpioValues[i + 1]);
            if (h == orep.HandleGpio) v = gr.GpioValues[i + 2];
        }
        SYS_CONSOLE_PRINT("PA%02u = %s\r\n", (unsigned)pin, v < 0 ? "?" : (v ? "HIGH (1)" : "LOW (0)"));
    } else {
        if (led_set(orep.HandleGpio, setVal))
            SYS_CONSOLE_PRINT("PA%02u driven %s\r\n", (unsigned)pin, setVal ? "HIGH (1)" : "LOW (0)");
        else
            SYS_CONSOLE_PRINT("SetGpio failed\r\n");
    }
done:
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

/* --- gpioevents <pin> [edge 0f/1r/2both] [secs] (mirrors gpioevents.c) ------
 * Arms input-edge capture and prints OnGpioEvents notifications. */
static int s_evseen;
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint64_t be64(const uint8_t *p) { uint64_t v = 0; int i; for (i = 0; i < 8; ++i) v = (v << 8) | p[i]; return v; }
static void on_gpio_events(const OnGpioEventsNotification_t *ev, void *ctx)
{
    static const char *tss[] = { "unsynced", "uncertain", "certain", "invalid" };
    uint16_t i;
    (void)ctx;
    for (i = 0; (uint16_t)(i + 13) <= ev->EventsLength; i += 13) {
        const uint8_t *r = &ev->Events[i];
        SYS_CONSOLE_PRINT("  [event %3d] handle=%u state=%s ts=%llu ns (%s)%s\r\n",
            ++s_evseen, (unsigned)be16(&r[0]), r[2] ? "HIGH" : "LOW",
            (unsigned long long)be64(&r[5]), tss[r[4] & 3], r[3] ? "  [OVERFLOW]" : "");
    }
}
static void cmd_gpioevents(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    ReleaseDigitalPinsVar_t rel; OpenGpioVar_t ov; OpenGpioReply_t orep;
    EnableGpioCaptureEventVar_t ce; DisableGpioEventVar_t de;
    uint8_t pin, trigger = 2u;
    uint32_t secs = 20u, start, endt;
    SYS_CONSOLE_HANDLE con = SYS_CONSOLE_HandleGet(SYS_CONSOLE_INDEX_0);
    int aborted = 0;
    (void)pCmdIO;

    if (argc < 2) { SYS_CONSOLE_PRINT("Usage: gpioevents <pin> [edge 0=fall 1=rise 2=both] [secs]\r\n"); return; }
    pin = (uint8_t)strtoul(argv[1], NULL, 10);
    if (argc >= 3) trigger = (uint8_t)strtoul(argv[2], NULL, 10);
    if (argc >= 4) secs    = (uint32_t)strtoul(argv[3], NULL, 10);
    if (pin > 15u) { SYS_CONSOLE_PRINT("pin must be 0..15\r\n"); return; }
    if (trigger > 2u) trigger = 2u;
    if (secs < 1u) secs = 1u; if (secs > 120u) secs = 120u;

    /* event delivery must be subscribed; the client may have inited without it. */
    rcp_enable_event_subscription(true);
    rcp_set_gpio_events_cb(on_gpio_events, NULL);

    if (!sel_first_ep()) { SYS_CONSOLE_PRINT("[gpioevents] no endpoint - run 'discovery' first\r\n"); return; }
    rcp_set_timeout_ms(800); rcp_set_retries(2);
    s_evseen = 0;

    memset(&rel, 0, sizeof(rel)); rel.PinIdList[0] = pin; rel.PinIdListLength = 1;
    rcp_release_digital_pins(&rel);
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdGpio = pin; ov.Direction = 0;   /* input */
    if (rcp_open_gpio(&ov, &orep) != RT_OK) { SYS_CONSOLE_PRINT("OpenGpio failed\r\n"); goto restore; }

    memset(&ce, 0, sizeof(ce));
    ce.HandleGpio = orep.HandleGpio; ce.NotificationType = 2; ce.Trigger = trigger; ce.Timestamped = 1;
    if (rcp_enable_gpio_capture_event(&ce) != RT_OK) {
        SYS_CONSOLE_PRINT("EnableGpioCaptureEvent failed - not supported by this firmware build\r\n");
        goto restore;
    }

    SYS_CONSOLE_PRINT("Watching PA%02u for %s edges for %u s (drive the pin; 'q' to stop)...\r\n",
                      (unsigned)pin, trigger == 2u ? "both" : trigger == 1u ? "rising" : "falling", (unsigned)secs);
    start = plat_now_ms(); endt = start + secs * 1000u;
    while (!aborted && (int32_t)(plat_now_ms() - endt) < 0) {
        rcp_poll();
        if (chk_abort(con)) aborted = 1;
        plat_sleep_ms(5);
    }
    memset(&de, 0, sizeof(de)); de.HandleGpio = orep.HandleGpio;
    rcp_disable_gpio_event(&de);
    SYS_CONSOLE_PRINT("\r\nDone (%d events).\r\n", s_evseen);
restore:
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

/* --- ledtoggle [pin] [beat_ms] [secs] : bounded async toggle (ledtoggle.c) -- */
static unsigned s_tgl_ok, s_tgl_to;
static void on_tgl(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{ (void)ctx; (void)rx; (void)rxLen; if (rc == RT_OK) s_tgl_ok++; else s_tgl_to++; }
static void cmd_ledtoggle(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    ReleaseDigitalPinsVar_t rel; OpenGpioVar_t ov; OpenGpioReply_t orep;
    uint8_t pin = 2u; uint32_t beat = 500u, secs = 10u, start, endt, nextT;
    int value = 0;
    SYS_CONSOLE_HANDLE con = SYS_CONSOLE_HandleGet(SYS_CONSOLE_INDEX_0);
    int aborted = 0;
    (void)pCmdIO;

    if (argc >= 2) pin  = (uint8_t)strtoul(argv[1], NULL, 10);
    if (argc >= 3) beat = (uint32_t)strtoul(argv[2], NULL, 10);
    if (argc >= 4) secs = (uint32_t)strtoul(argv[3], NULL, 10);
    if (pin > 15u) pin = 2u;
    if (beat < 20u) beat = 20u; if (beat > 5000u) beat = 5000u;
    if (secs < 1u) secs = 1u; if (secs > 600u) secs = 600u;

    if (!sel_first_ep()) { SYS_CONSOLE_PRINT("[ledtoggle] no endpoint - run 'discovery' first\r\n"); return; }
    rcp_set_timeout_ms(800); rcp_set_retries(2);
    memset(&rel, 0, sizeof(rel)); rel.PinIdList[0] = pin; rel.PinIdListLength = 1;
    rcp_release_digital_pins(&rel); plat_sleep_ms(20);
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdGpio = pin; ov.Direction = 1;
    if (rcp_open_gpio(&ov, &orep) != RT_OK) { SYS_CONSOLE_PRINT("OpenGpio failed\r\n"); goto restore; }

    s_tgl_ok = s_tgl_to = 0;
    rcp_set_async_timeout_ms(beat < 300u ? beat : 300u);
    SYS_CONSOLE_PRINT("Toggling PA%02u every %u ms for %u s ('q' to stop)...\r\n",
                      (unsigned)pin, (unsigned)beat, (unsigned)secs);
    start = plat_now_ms(); endt = start + secs * 1000u; nextT = start;
    while (!aborted && (int32_t)(plat_now_ms() - endt) < 0) {
        if ((int32_t)(plat_now_ms() - nextT) >= 0) {
            uint8_t p[16]; uint16_t n;
            value = !value;
            n = rcp_enc_gpio_set(p, sizeof(p), orep.HandleGpio, (uint8_t)value);
            if (n) rcp_async_request(0x1330u, p, n, on_tgl, NULL);
            nextT += beat;
        }
        rcp_async_poll();
        if (chk_abort(con)) aborted = 1;
        plat_sleep_ms(2);
    }
    led_set(orep.HandleGpio, 0);
    rcp_set_async_timeout_ms(150);
    SYS_CONSOLE_PRINT("\r\nStopped (ok=%u to=%u). PA%02u off.\r\n", s_tgl_ok, s_tgl_to, (unsigned)pin);
restore:
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

/* --- ledpwm [pin] [freqHz] [breathMs] [secs] : breathing PWM (ledpwm.c) -----
 * PWM may be unimplemented on a Lighting build; OpenPwm then fails cleanly. */
static unsigned s_pwm_ok;
static void on_pwm(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{ (void)ctx; (void)rx; (void)rxLen; if (rc == RT_OK) s_pwm_ok++; }
static uint32_t duty_q31(int permille)   /* 0..1000 -> 0..2^31 */
{
    if (permille < 0) permille = 0; if (permille > 1000) permille = 1000;
    return (uint32_t)(((uint64_t)permille * 2147483648ULL) / 1000ULL);
}
static void cmd_ledpwm(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    ReleaseDigitalPinsVar_t rel; OpenPwmVar_t ov; OpenPwmReply_t orep; ClosePwmVar_t cl;
    uint8_t pin = 2u; uint32_t freq = 1000u, breathMs = 2000u, secs = 15u;
    uint32_t periodNs, start, endt, nextT, stepMs; int steps = 50, step = 0, dir = 1;
    uint16_t handle; uint32_t wid = 0u;
    SYS_CONSOLE_HANDLE con = SYS_CONSOLE_HandleGet(SYS_CONSOLE_INDEX_0);
    int aborted = 0;
    (void)pCmdIO;

    if (argc >= 2) pin      = (uint8_t)strtoul(argv[1], NULL, 10);
    if (argc >= 3) freq     = (uint32_t)strtoul(argv[2], NULL, 10);
    if (argc >= 4) breathMs = (uint32_t)strtoul(argv[3], NULL, 10);
    if (argc >= 5) secs     = (uint32_t)strtoul(argv[4], NULL, 10);
    if (pin > 15u) pin = 2u;
    if (freq < 1u) freq = 1000u;
    if (breathMs < 200u) breathMs = 200u;
    if (secs < 1u) secs = 1u; if (secs > 600u) secs = 600u;
    periodNs = (uint32_t)(1000000000ULL / freq);
    stepMs = breathMs / (uint32_t)(2 * steps); if (stepMs < 1u) stepMs = 1u;

    if (!sel_first_ep()) { SYS_CONSOLE_PRINT("[ledpwm] no endpoint - run 'discovery' first\r\n"); return; }
    rcp_set_timeout_ms(800); rcp_set_retries(2);
    memset(&rel, 0, sizeof(rel)); rel.PinIdList[0] = pin; rel.PinIdListLength = 1;
    rcp_release_digital_pins(&rel); plat_sleep_ms(20);
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinId = pin; ov.IntervalTime = periodNs; ov.DutyCycle = 0;
    if (rcp_open_pwm(&ov, &orep) != RT_OK) {
        SYS_CONSOLE_PRINT("OpenPwm failed - PWM likely not implemented on this firmware build.\r\n");
        goto restore;
    }
    handle = orep.HandlePwm;

    s_pwm_ok = 0;
    rcp_set_async_timeout_ms(200);
    SYS_CONSOLE_PRINT("Breathing PA%02u (%u Hz, %u ms/breath) for %u s ('q' to stop)...\r\n",
                      (unsigned)pin, (unsigned)freq, (unsigned)breathMs, (unsigned)secs);
    start = plat_now_ms(); endt = start + secs * 1000u; nextT = start;
    while (!aborted && (int32_t)(plat_now_ms() - endt) < 0) {
        if ((int32_t)(plat_now_ms() - nextT) >= 0) {
            uint8_t pp[32]; uint16_t n;
            int permille = 1000 * step / steps;
            n = rcp_enc_pwm_write(pp, sizeof(pp), handle, wid++, duty_q31(permille));
            if (n) rcp_async_request(0x1804u, pp, n, on_pwm, NULL);
            step += dir;
            if (step >= steps) { step = steps; dir = -1; }
            else if (step <= 0) { step = 0; dir = 1; }
            nextT += stepMs;
        }
        rcp_async_poll();
        if (chk_abort(con)) aborted = 1;
        plat_sleep_ms(2);
    }
    { uint8_t pp[32]; uint16_t n = rcp_enc_pwm_write(pp, sizeof(pp), handle, wid++, 0u);
      if (n) { rcp_async_request(0x1804u, pp, n, on_pwm, NULL); rcp_async_poll(); plat_sleep_ms(20); rcp_async_poll(); } }
    memset(&cl, 0, sizeof(cl)); cl.HandlePwm = handle; rcp_close_pwm(&cl);
    rcp_set_async_timeout_ms(150);
    SYS_CONSOLE_PRINT("\r\nStopped (writes ok=%u). PWM closed.\r\n", s_pwm_ok);
restore:
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

static const SYS_CMD_DESCRIPTOR gpio_cmd_tbl[] = {
    {"gpio",       (SYS_CMD_FNC) cmd_gpio,       ": set/read a GPIO (gpio <pin> <0|1|get>)"},
    {"gpioevents", (SYS_CMD_FNC) cmd_gpioevents, ": live GPIO edge events (gpioevents <pin> [edge] [secs])"},
    {"ledtoggle",  (SYS_CMD_FNC) cmd_ledtoggle,  ": bounded async LED toggle (ledtoggle [pin] [beat_ms] [secs])"},
    {"ledpwm",     (SYS_CMD_FNC) cmd_ledpwm,     ": breathing LED via PWM (ledpwm [pin] [freqHz] [breathMs] [secs])"},
};

void GPIO_CLI_Init(void)
{
    SYS_CMD_ADDGRP(gpio_cmd_tbl, sizeof(gpio_cmd_tbl) / sizeof(*gpio_cmd_tbl), "gpio", ": GPIO/LED demos");
}
