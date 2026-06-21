/*
 * uart.c  -  Open a UART on a LAN866x endpoint, write and/or read bytes. Pure C.
 *
 *   lan866x-uart --tx 0 --rx 3 --baud 115200 --write "hello\r\n"
 *   lan866x-uart --tx 0 --rx 3 --read                 poll once for buffered RX
 *   lan866x-uart --tx 0 --rx 3 --listen               live RX via notifications
 *   lan866x-uart --ip 192.168.0.54 --tx 0 --rx 3 --write "AT\r"
 *
 * --listen demonstrates the UART RX *event* path: the endpoint is opened with
 * Notification=1 and pushes OnUartReceive (0x8010) as data arrives, delivered
 * to a callback - no polling. Pins default to PA00 (TX) / PA03 (RX).
 *
 * NOTE: UART wrappers are wired from the v1.10.0 proto and not yet verified on
 * hardware here - confirm against your firmware build.
 */
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

static volatile int s_rxTotal = 0;

static void print_bytes(const char *label, const uint8_t *d, uint16_t n)
{
    uint16_t i;
    printf("  %s %u byte(s): \"", label, n);
    for (i = 0; i < n; ++i) putchar((d[i] >= 0x20 && d[i] < 0x7F) ? d[i] : '.');
    printf("\"  [");
    for (i = 0; i < n; ++i) printf("%02X%s", d[i], i + 1 < n ? " " : "");
    printf("]\n");
    fflush(stdout);
}

static void on_uart_rx(const OnUartReceiveNotification_t *ev, void *ctx)
{
    (void)ctx;
    s_rxTotal += ev->ReadDataLength;
    printf("  [rx id=%u]", (unsigned)ev->ReadId);
    print_bytes("", ev->ReadData, ev->ReadDataLength);
}

/* turn a "hello\r\n"-style arg (with \r \n \t \\ escapes) into raw bytes */
static uint16_t unescape(const char *s, uint8_t *out, uint16_t cap)
{
    uint16_t n = 0;
    while (*s && n < cap) {
        if (*s == '\\' && s[1]) {
            ++s;
            out[n++] = (*s == 'r') ? '\r' : (*s == 'n') ? '\n' :
                       (*s == 't') ? '\t' : (uint8_t)*s;
        } else out[n++] = (uint8_t)*s;
        ++s;
    }
    return n;
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL, *writeStr = NULL;
    int wantEp = 0, i, tx = 0, rx = 3, doRead = 0, doListen = 0;
    uint32_t baud = 115200;
    OpenUartVar_t ov; OpenUartReply_t orep;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-uart - open a UART, write/read bytes (pure C)\n\n"
                   "  --tx <0..15>     TX pin PA00..PA15 (default 0)\n"
                   "  --rx <0..15>     RX pin PA00..PA15 (default 3)\n"
                   "  --baud <n>       baud rate (default 115200)\n"
                   "  --write <str>    bytes to send (\\r \\n \\t escapes ok)\n"
                   "  --read           poll once for buffered RX\n"
                   "  --listen         live RX via notifications (Ctrl+C to stop)\n"
                   "  --ip <addr> | --ep <i>   target endpoint\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")    && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")    && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tx")    && i+1<argc) tx = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rx")    && i+1<argc) rx = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--baud")  && i+1<argc) baud = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--write") && i+1<argc) writeStr = argv[++i];
        else if (!strcmp(argv[i], "--read"))   doRead = 1;
        else if (!strcmp(argv[i], "--listen")) doListen = 1;
    }
    if (!writeStr && !doRead && !doListen) {
        printf("Nothing to do. Pass --write, --read and/or --listen. Use --help.\n");
        return 1;
    }

    rcp_enable_event_subscription(doListen ? true : false);  /* before rcp_init */
    if (doListen) rcp_set_uart_receive_cb(on_uart_rx, NULL);

    if (tool_select(wantIp, wantEp, 5, "LAN866x UART tool (pure C)") < 0) return 2;

    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdTx = (uint8_t)tx; ov.PinIdRx = (uint8_t)rx;
    ov.PinIdRts = 0xFF; ov.PinIdCts = 0xFF;
    ov.Notification = doListen ? 1u : 0u;   /* 1 = push RX as OnUartReceive */
    ov.BaudRate = baud;
    ov.Parity = 2;     /* none */
    ov.StopBits = 0;   /* one */
    ov.BitOrder = 0;   /* little-endian */
    ov.RxBufferSize = 256;
    ov.RxThreshold = 1;
    ov.RxTimeout = 10;
    if (rcp_open_uart(&ov, &orep) != RT_OK) { printf("OpenUart failed.\n"); return 3; }
    printf("UART open: TX=PA%02d RX=PA%02d @ %lu Bd  (handle %u)\n",
           tx, rx, (unsigned long)baud, orep.HandleUart);

    if (writeStr) {
        WriteUartVar_t wv; memset(&wv, 0, sizeof(wv));
        wv.HandleUart = orep.HandleUart; wv.WriteId = 0;
        wv.WriteDataLength = unescape(writeStr, wv.WriteData, sizeof(wv.WriteData));
        if (rcp_write_uart(&wv) != RT_OK) { printf("WriteUart failed.\n"); return 4; }
        print_bytes("TX", wv.WriteData, wv.WriteDataLength);
    }

    if (doRead) {
        ReadUartVar_t rv; ReadUartReply_t rr;
        memset(&rv, 0, sizeof(rv)); memset(&rr, 0, sizeof(rr));
        rv.HandleUart = orep.HandleUart;
        if (rcp_read_uart(&rv, &rr) != RT_OK) { printf("ReadUart failed.\n"); return 5; }
        if (rr.ReadDataLength) print_bytes("RX", rr.ReadData, rr.ReadDataLength);
        else printf("  RX: (nothing buffered)\n");
    }

    if (doListen) {
        printf("\nListening for RX on PA%02d (Ctrl+C to stop) ...\n\n", rx);
        for (;;) { rcp_poll(); Sleep(5); }  /* delivers OnUartReceive to on_uart_rx() */
    }
    return 0;
}
