/*
 * video.c  -  Play a video file on a LAN866x lighting board's two RGB displays.
 *             Pure C, Windows.
 *
 * ffmpeg decodes + scales the file to a 20x10 RGB stream (and loops it); this
 * tool reads the raw frames and sends each as one RTP/RFC4175 frame to UDP 5001,
 * which the lighting firmware renders onto the two 10x10 WS2812 displays
 * (left half = display 1, right half = display 2).
 *
 *   lan866x-video docs/img/clickdemo.mp4 --ip 192.168.0.54
 *
 * Requires ffmpeg in PATH (or pass --ffmpeg <path>). Ctrl-C to stop.
 */

#include <winsock2.h>     /* before any windows.h pulled in by tool_common.h */
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "rcp.h"
#include "tool_common.h"
#include <mmsystem.h>     /* timeBeginPeriod: 1 ms tick for smoother pacing */

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

#define X_RES    20
#define Y_RES    10
#define RTP_PORT 5001
#define RTP_TYPE 96
#define FRAME_BYTES (X_RES * Y_RES * 3)

static uint8_t  s_fb[Y_RES][X_RES][3];
static SOCKET   s_rtp = INVALID_SOCKET;
static uint8_t  s_ip[4];
static uint32_t s_seq = 0, s_ssrc = 0;
static volatile int s_run = 1;

static void on_sigint(int sig) { (void)sig; s_run = 0; }

static uint32_t now_us10(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint32_t)(((c.QuadPart * 1000000ULL) / (uint64_t)f.QuadPart) / 10ULL);
}

/* send the whole 20x10 frame as one RTP/RFC4175 packet (marker set) */
static void rtp_send(void)
{
    uint8_t pkt[64 + Y_RES * 6 + FRAME_BYTES];
    int n = 0, x, y;
    uint32_t ts = now_us10();
    struct sockaddr_in dst;

    pkt[n++] = 0x80;
    pkt[n++] = (uint8_t)(0x80u | (RTP_TYPE & 0x7Fu));
    pkt[n++] = (uint8_t)(s_seq >> 8);  pkt[n++] = (uint8_t)s_seq;
    pkt[n++] = (uint8_t)(ts >> 24); pkt[n++] = (uint8_t)(ts >> 16);
    pkt[n++] = (uint8_t)(ts >> 8);  pkt[n++] = (uint8_t)ts;
    pkt[n++] = (uint8_t)(s_ssrc >> 24); pkt[n++] = (uint8_t)(s_ssrc >> 16);
    pkt[n++] = (uint8_t)(s_ssrc >> 8);  pkt[n++] = (uint8_t)s_ssrc;
    pkt[n++] = (uint8_t)(s_seq >> 24); pkt[n++] = (uint8_t)(s_seq >> 16);

    for (y = 0; y < Y_RES; ++y) {
        int cont = (y != (Y_RES - 1));
        uint16_t len = (uint16_t)(X_RES * 3);
        pkt[n++] = (uint8_t)(len >> 8); pkt[n++] = (uint8_t)len;
        pkt[n++] = (uint8_t)(y >> 8);   pkt[n++] = (uint8_t)y;
        pkt[n++] = (uint8_t)(cont ? 0x80 : 0x00); pkt[n++] = 0x00;
    }
    for (y = 0; y < Y_RES; ++y)
        for (x = 0; x < X_RES; ++x) {
            pkt[n++] = s_fb[y][x][0]; pkt[n++] = s_fb[y][x][1]; pkt[n++] = s_fb[y][x][2];
        }

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(RTP_PORT);
    dst.sin_addr.s_addr = (uint32_t)s_ip[0] | ((uint32_t)s_ip[1] << 8) |
                          ((uint32_t)s_ip[2] << 16) | ((uint32_t)s_ip[3] << 24);
    sendto(s_rtp, (const char *)pkt, n, 0, (struct sockaddr *)&dst, sizeof(dst));
    s_seq++;
}

/* read exactly n bytes from the pipe (frames can arrive in pieces) */
static int read_full(FILE *p, uint8_t *buf, int n)
{
    int got = 0; size_t r;
    while (got < n) {
        r = fread(buf + got, 1, (size_t)(n - got), p);
        if (r == 0) return 0;            /* EOF / ffmpeg ended */
        got += (int)r;
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *file = NULL, *wantIp = NULL, *ffmpeg = "ffmpeg";
    int wantEp = 0, i, fps = 15, bright = 128, sel;
    rcp_endpoint_t eps[RCP_MAX_ENDPOINTS];
    WSADATA wsa;
    char cmd[1024];
    FILE *pipe;
    uint8_t raw[FRAME_BYTES];

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-video - loop-play a video on the board's RGB displays (pure C)\n\n"
                   "  lan866x-video <file> [--ip <addr>|--ep <i>] [--fps N] [--bright 0..255] [--ffmpeg <path>]\n\n"
                   "  ffmpeg decodes+scales the file to 20x10 and loops it; each frame is sent as\n"
                   "  one RTP frame to UDP %d (left half=display 1, right half=display 2).\n", RTP_PORT);
            return 0;
        } else if (!strcmp(argv[i], "--ip")     && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")     && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fps")    && i+1<argc) fps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bright") && i+1<argc) bright = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ffmpeg") && i+1<argc) ffmpeg = argv[++i];
        else if (argv[i][0] != '-') file = argv[i];
    }
    if (!file) { printf("Need a video file. Use --help.\n"); return 1; }
    if (fps < 1) fps = 1; if (fps > 60) fps = 60;
    if (bright < 1) bright = 1; if (bright > 255) bright = 255;

    sel = tool_select(wantIp, wantEp, 5, "LAN866x video player");
    if (sel < 0) return 2;
    rcp_get_endpoints(eps, RCP_MAX_ENDPOINTS);
    memcpy(s_ip, eps[sel].ip, 4);

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { printf("WSAStartup failed\n"); return 3; }
    s_rtp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_rtp == INVALID_SOCKET) { printf("socket failed\n"); return 3; }
    s_ssrc = (uint32_t)GetTickCount();
    signal(SIGINT, on_sigint);
    timeBeginPeriod(1);

    /* ffmpeg: loop forever, scale to 20x10, emit raw rgb24 frames at <fps> */
    snprintf(cmd, sizeof(cmd),
             "%s -hide_banner -loglevel error -stream_loop -1 -i \"%s\" -an "
             "-vf scale=%d:%d,fps=%d -f rawvideo -pix_fmt rgb24 -",
             ffmpeg, file, X_RES, Y_RES, fps);
    pipe = _popen(cmd, "rb");
    if (!pipe) { printf("Cannot start ffmpeg. Is it in PATH? (use --ffmpeg)\n"); return 4; }

    printf("Playing %s -> %u.%u.%u.%u:%d  (%d fps, loop, bright %d). Ctrl-C to stop.\n",
           file, s_ip[0], s_ip[1], s_ip[2], s_ip[3], RTP_PORT, fps, bright);

    {
        unsigned long frames = 0;
        while (s_run && read_full(pipe, raw, FRAME_BYTES)) {
            int x, y, k = 0;
            for (y = 0; y < Y_RES; ++y)
                for (x = 0; x < X_RES; ++x) {
                    s_fb[y][x][0] = (uint8_t)(raw[k++] * bright / 255);
                    s_fb[y][x][1] = (uint8_t)(raw[k++] * bright / 255);
                    s_fb[y][x][2] = (uint8_t)(raw[k++] * bright / 255);
                }
            rtp_send();
            if ((++frames % (unsigned long)fps) == 0) { printf("\r  frames: %lu  ", frames); fflush(stdout); }
            Sleep((DWORD)(1000 / fps));
        }
    }

    printf("\nStopping - clearing displays ...\n");
    memset(s_fb, 0, sizeof(s_fb));
    rtp_send(); Sleep(30); rtp_send();
    timeEndPeriod(1);
    _pclose(pipe);
    closesocket(s_rtp); WSACleanup();
    return 0;
}
