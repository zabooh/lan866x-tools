/*
 * flashpkg.c  -  Update a LAN866x endpoint from an MCHPKG package. Pure C.
 *                "Stage 3": you hand it a .mchpkg, it extracts the main app +
 *                main config images itself and flashes both.
 *
 *   lan866x-flashpkg LAN8661-ws2812_V1.3.2_RELEASE_display1.mchpkg --ip 192.168.0.54
 *
 * Flow: open the package (ZIP) -> read main/app.{bin,iv,signature} and
 * main/config.{bin,iv,signature} -> reboot the endpoint to the bootloader ->
 * flash main/app then main/config (StartUpdate/WriteImage/FinishUpdate) ->
 * reboot to the main app -> verify via GetStatus.
 *
 *   !! Writes flash. Use a package whose chip/app matches the target. Recoverable
 *      from the bootloader if a write is interrupted.
 *   Note: bootloader/keys/factory upgrades (updater/*) are NOT handled here -
 *   this does the normal firmware+config update only.
 */
#include <stdio.h>
#include <stdlib.h>
#include "rcp.h"
#include "tool_common.h"
#include "unzip.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

/* --- minizip file I/O callbacks (from the official libflash) ------------- */
static void *myOpen(const char *filename, int32_t *size)
{
    FILE *f = fopen(filename, "rb");
    if (f) { fseek(f, 0, SEEK_END); *size = (int32_t)ftell(f); fseek(f, 0, SEEK_SET); }
    return (void *)f;
}
static void myClose(void *p) { ZIPFILE *z = (ZIPFILE *)p; if (z) fclose((FILE *)z->fHandle); }
static int32_t myRead(void *p, uint8_t *b, int32_t n) { ZIPFILE *z = (ZIPFILE *)p; return z ? (int32_t)fread(b, 1, n, (FILE *)z->fHandle) : 0; }
static int32_t mySeek(void *p, int32_t pos, int t) { ZIPFILE *z = (ZIPFILE *)p; return z ? fseek((FILE *)z->fHandle, pos, t) : 0; }

/* extract one zip entry into a malloc'd buffer */
static uint8_t *zip_extract(unzFile h, const char *name, uint32_t *outLen)
{
    unz_file_info fi; uint8_t *buf; int rc;
    if (unzLocateFile(h, name, 2) != UNZ_OK) return NULL;
    if (unzGetCurrentFileInfo(h, &fi, NULL, 0, NULL, 0, NULL, 0) != UNZ_OK) return NULL;
    if (unzOpenCurrentFile(h) != UNZ_OK) return NULL;
    buf = (uint8_t *)malloc(fi.uncompressed_size ? fi.uncompressed_size : 1);
    if (!buf) { unzCloseCurrentFile(h); return NULL; }
    rc = unzReadCurrentFile(h, buf, (int)fi.uncompressed_size);
    unzCloseCurrentFile(h);
    if (rc < 0 || (uint32_t)rc != fi.uncompressed_size) { free(buf); return NULL; }
    *outLen = (uint32_t)rc;
    return buf;
}

/* --- shared flash/reboot helpers ---------------------------------------- */
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
    printf("\nRebooting into %s ...\n", label);
    rcp_set_retries(0); (void)rcp_reboot(image); rcp_set_retries(3);
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

/* flash one image given its three blobs (logical name e.g. "main/app.bin") */
static ReturnCode_t flash_one(const char *logical,
                              const uint8_t *d, uint32_t dl, const uint8_t *iv, uint32_t il,
                              const uint8_t *sg, uint32_t sl)
{
    ReturnCode_t rc;
    printf("\nFlashing %s (%u bytes) ...\n", logical, dl);
    rc = rcp_flash_image(logical, d, dl, (uint8_t *)iv, (uint16_t)il, (uint8_t *)sg, (uint16_t)sl, progress);
    printf("\n  -> %s (rc=%d)\n", rc == RT_OK ? "OK" : "FAILED", rc);
    return rc;
}

int main(int argc, char **argv)
{
    const char *pkg = NULL, *wantIp = NULL;
    int wantEp = 0, i, waitS = 25, chunk = 1200, retries = 15, doApp = 1, doCfg = 1;
    uint8_t *app=NULL,*appIv=NULL,*appSig=NULL,*cfg=NULL,*cfgIv=NULL,*cfgSig=NULL;
    uint32_t appL=0,appIvL=0,appSigL=0,cfgL=0,cfgIvL=0,cfgSigL=0;
    static ZIPFILE zpf;
    unzFile h;
    ReturnCode_t rc = RT_OK;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-flashpkg - update an endpoint from an MCHPKG package (pure C)\n\n"
                   "  lan866x-flashpkg <package.mchpkg> [--ip <addr>|--ep <i>]\n"
                   "      [--config-only] [--app-only] [--chunk N] [--retries N] [--wait s]\n\n"
                   "  Extracts main/app + main/config from the package and flashes both\n"
                   "  (firmware + config update). WRITES FLASH; recoverable from bootloader.\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")          && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")          && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--wait")        && i+1<argc) waitS = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--chunk")       && i+1<argc) chunk = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--retries")     && i+1<argc) retries = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--config-only")) doApp = 0;
        else if (!strcmp(argv[i], "--app-only"))    doCfg = 0;
        else if (argv[i][0] != '-') pkg = argv[i];
    }
    if (!pkg) { printf("Need a package file. Use --help.\n"); return 1; }

    /* --- open package and extract the images --- */
    memset(&zpf, 0, sizeof(zpf));
    h = unzOpen(pkg, NULL, 0, &zpf, myOpen, myRead, mySeek, myClose);
    if (!h) { printf("Cannot open package: %s\n", pkg); return 1; }
    if (doApp) {
        app    = zip_extract(h, "main/app.bin", &appL);
        appIv  = zip_extract(h, "main/app.iv.bin", &appIvL);
        appSig = zip_extract(h, "main/app.signature.bin", &appSigL);
    }
    if (doCfg) {
        cfg    = zip_extract(h, "main/config.bin", &cfgL);
        cfgIv  = zip_extract(h, "main/config.iv.bin", &cfgIvL);
        cfgSig = zip_extract(h, "main/config.signature.bin", &cfgSigL);
    }
    unzClose(h);

    if (doApp && (!app || !appIv || !appSig)) { printf("Package is missing main/app images.\n"); return 1; }
    if (doCfg && (!cfg || !cfgIv || !cfgSig)) { printf("Package is missing main/config images.\n"); return 1; }
    printf("Package: %s\n", pkg);
    if (doApp) printf("  main/app:    %u B (iv %u, sig %u)\n", appL, appIvL, appSigL);
    if (doCfg) printf("  main/config: %u B (iv %u, sig %u)\n", cfgL, cfgIvL, cfgSigL);

    /* --- select target and flash --- */
    if (tool_select(wantIp, wantEp, 5, "LAN866x package flasher (pure C)") < 0) return 2;
    show_status("before");

    if (!reboot_to(RCP_IMAGE_BOOTLOADER, "bootloader", waitS)) return 3;
    show_status("bootloader");

    rcp_set_chunk((uint16_t)chunk);
    rcp_set_retries((uint8_t)retries);
    if (doApp && rc == RT_OK) rc = flash_one("main/app.bin", app, appL, appIv, appIvL, appSig, appSigL);
    if (doCfg && rc == RT_OK) rc = flash_one("main/config.bin", cfg, cfgL, cfgIv, cfgIvL, cfgSig, cfgSigL);
    rcp_set_retries(3); rcp_set_timeout_ms(1500);

    if (!reboot_to(RCP_IMAGE_MAIN, "main app", waitS))
        printf("  ! app did not come back - the endpoint is in the bootloader (recoverable: re-run).\n");
    else
        show_status("after");

    free(app); free(appIv); free(appSig); free(cfg); free(cfgIv); free(cfgSig);
    return (rc == RT_OK) ? 0 : 4;
}
