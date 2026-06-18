/*
 * flashimg.c  -  Write ONE pre-built (signed/encrypted) image to a LAN866x
 *                endpoint via the bootloader. Pure C. "Stage 2" of the flasher.
 *
 *   reboot -> bootloader ; StartUpdate(name,IV) ; WriteImage chunks ;
 *   FinishUpdate(name,signature) ; reboot -> main ; verify (GetStatus)
 *
 * The 3 blobs are taken straight from an MCHPKG (e.g. main/config.bin +
 * main/config.iv.bin + main/config.signature.bin). The host only transports
 * them; the bootloader verifies the signature.
 *
 *   !! This WRITES flash. Use signed images from a matching package. A failed
 *      write is recoverable (re-flash from the bootloader).
 *
 * Usage:
 *   lan866x-flashimg --ip 192.168.0.54 --image main/config.bin \
 *       --data config.bin --iv config.iv.bin --sig config.signature.bin
 */
#include <stdio.h>
#include <stdlib.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

static uint8_t *read_file(const char *path, uint32_t *len)
{
    FILE *f = fopen(path, "rb");
    long n; uint8_t *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    b = (uint8_t *)malloc((size_t)n);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) *len = (uint32_t)n;
    return b;
}

static void show_status(const char *when)
{
    GetStatusReply_t st; memset(&st, 0, sizeof(st));
    if (rcp_get_status(&st) == RT_OK)
        printf("  [%s] App=%s  Chip=%s  Main=%s  Svc=0x%08X\n",
               when, st.ActiveApplication, st.ChipIdentifier, st.MainApplicationVersion, st.ServiceVersion);
    else
        printf("  [%s] GetStatus failed\n", when);
}

static long long get_uptime(void)
{
    GetStatusReply_t st; memset(&st, 0, sizeof(st));
    if (rcp_get_status(&st) == RT_OK) return (long long)(st.UpTime / 1000000000ULL);
    return -1;
}

static int wait_up(int timeoutS)
{
    int i, ok = 0;
    rcp_set_retries(0); rcp_set_timeout_ms(800);
    for (i = 0; i < timeoutS; ++i) {
        GetStatusReply_t st; memset(&st, 0, sizeof(st));
        if (rcp_get_status(&st) == RT_OK) { ok = 1; break; }
        rcp_poll(); Sleep(300);
    }
    rcp_set_retries(3); rcp_set_timeout_ms(1500);
    return ok;
}

static int reboot_to(const char *image, const char *label, int waitS)
{
    long long before = get_uptime(), after;
    ReturnCode_t rc;
    printf("\nRebooting into %s ...\n", label);
    rcp_set_retries(0); rc = rcp_reboot(image); rcp_set_retries(3);
    printf("  reboot rc=%d\n", rc);
    Sleep(2500);
    if (!wait_up(waitS)) { printf("  ! endpoint did not reappear within %d s\n", waitS); return 0; }
    after = get_uptime();
    printf("  -> %s (uptime %llds -> %llds)\n",
           (after >= 0 && (before < 0 || after < before)) ? "rebooted" : "NOT reset", before, after);
    return 1;
}

static void progress(uint32_t done, uint32_t total)
{
    printf("\r  writing %u / %u bytes (%u%%)   ", done, total, total ? (unsigned)(done * 100u / total) : 0u);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL, *image = "main/config.bin";
    const char *fData = NULL, *fIv = NULL, *fSig = NULL;
    int wantEp = 0, i, waitS = 25, chunk = 256, retries = 20;
    uint8_t *data = NULL, *iv = NULL, *sig = NULL;
    uint32_t dataLen = 0, ivLen = 0, sigLen = 0;
    ReturnCode_t rc;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-flashimg - write one signed image via the bootloader (pure C)\n"
                   "  --image <name>   logical image name (default main/config.bin)\n"
                   "  --data <file>    image .bin\n  --iv <file>  .iv.bin\n  --sig <file>  .signature.bin\n"
                   "  --ip/--ep        target endpoint    --wait <s>  reappear timeout\n"
                   "  WRITES FLASH. Use signed images from a matching MCHPKG.\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")    && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")    && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--image") && i+1<argc) image = argv[++i];
        else if (!strcmp(argv[i], "--data")  && i+1<argc) fData = argv[++i];
        else if (!strcmp(argv[i], "--iv")    && i+1<argc) fIv = argv[++i];
        else if (!strcmp(argv[i], "--sig")   && i+1<argc) fSig = argv[++i];
        else if (!strcmp(argv[i], "--wait")    && i+1<argc) waitS = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--chunk")   && i+1<argc) chunk = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--retries") && i+1<argc) retries = atoi(argv[++i]);
    }
    if (!fData || !fIv || !fSig) { printf("Need --data, --iv and --sig. Use --help.\n"); return 1; }

    data = read_file(fData, &dataLen);
    iv   = read_file(fIv,   &ivLen);
    sig  = read_file(fSig,  &sigLen);
    if (!data || !iv || !sig) { printf("Failed to read one of the input files.\n"); return 1; }
    printf("Image '%s': data=%u B, iv=%u B, signature=%u B\n", image, dataLen, ivLen, sigLen);

    if (tool_select(wantIp, wantEp, 5, "LAN866x flash-image tool (pure C)") < 0) return 2;
    show_status("before");

    if (!reboot_to(RCP_IMAGE_BOOTLOADER, "bootloader", waitS)) return 3;
    show_status("bootloader");

    printf("\nFlashing image '%s' (chunk=%d, retries=%d) ...\n", image, chunk, retries);
    /* The T1S link can drop packets; small chunks + many retries push through.
     * Resends are safe: WriteId makes a repeated chunk idempotent. */
    rcp_set_chunk((uint16_t)chunk);
    rcp_set_retries((uint8_t)retries);
    rcp_set_timeout_ms(3000);
    rc = rcp_flash_image(image, data, dataLen, (uint8_t *)iv, (uint16_t)ivLen, (uint8_t *)sig, (uint16_t)sigLen, progress);
    rcp_set_retries(3);
    rcp_set_timeout_ms(1500);
    printf("\n  flash result: rc=%d (%s)\n", rc, rc == RT_OK ? "OK" : "FAILED");

    if (!reboot_to(RCP_IMAGE_MAIN, "main app", waitS)) { printf("  ! app did not come back - recover from bootloader\n"); return 4; }
    show_status("after");

    free(data); free(iv); free(sig);
    return (rc == RT_OK) ? 0 : 4;
}
