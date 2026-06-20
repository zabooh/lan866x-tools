/*
 * boot.c  -  Reboot a LAN866x endpoint between its main app and the bootloader
 *            and show the status in each mode. Pure C. NON-DESTRUCTIVE:
 *            it only issues Reboot (0x1000) - it never writes/erases flash.
 *
 * This is "stage 1" of a flash tool: it validates the reboot + re-discovery
 * path (and lets you inspect the bootloader, e.g. its version/name).
 *
 * Usage:
 *   lan866x-boot                         cycle: app -> bootloader -> app  (default)
 *   lan866x-boot --to bootloader         reboot into the bootloader and stay
 *   lan866x-boot --to main               reboot into the main app
 *   lan866x-boot --ip 192.168.0.54 --wait 20
 */
#include <stdlib.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

static void show_status(const char *when)
{
    GetStatusReply_t st;
    memset(&st, 0, sizeof(st));
    if (rcp_get_status(&st) == RT_OK) {
        unsigned long long up = st.UpTime / 1000000000ULL;
        printf("  [%s]\n", when);
        printf("        Application: %s\n", st.ActiveApplication);
        printf("        Chip:        %s\n", st.ChipIdentifier);
        printf("        Main ver:    %s\n", st.MainApplicationVersion);
        printf("        Boot ver:    %s\n", st.BootApplicationVersion);
        printf("        Service:     0x%08X   Uptime: %llus\n", st.ServiceVersion, up);
    } else {
        printf("  [%s] GetStatus failed\n", when);
    }
}

static long long get_uptime(void)
{
    GetStatusReply_t st; memset(&st, 0, sizeof(st));
    if (rcp_get_status(&st) == RT_OK) return (long long)(st.UpTime / 1000000000ULL);
    return -1;
}

/* Wait for the (rebooted) endpoint to come back - re-acquiring it by SOME/IP-SD
 * so a changed IP (bootloader config IP != main app IP) is handled, not just the
 * same-IP case. */
static int wait_up(int timeoutS) { return tool_reacquire(timeoutS) >= 0; }

static int reboot_to(const char *image, const char *label, int waitS)
{
    ReturnCode_t rc;
    long long before, after;
    before = get_uptime();
    printf("\nRebooting into %s (\"%s\") ...\n", label, image);
    rcp_set_retries(0);            /* device acks then resets - don't retry */
    rc = rcp_reboot(image);
    rcp_set_retries(3);
    printf("  reboot request rc=%d (%s)\n", rc,
           rc == RT_OK ? "acknowledged" : "no ack - may still have rebooted");
    Sleep(2500);                   /* give the device time to actually reset */
    printf("  waiting for the endpoint to come back ...\n");
    if (!wait_up(waitS)) { printf("  ! endpoint did not reappear within %d s\n", waitS); return 0; }
    after = get_uptime();
    if (after >= 0 && (before < 0 || after < before))
        printf("  -> device REBOOTED (uptime %llds -> %llds)\n", before, after);
    else
        printf("  -> NOTE: device did NOT reset (uptime %llds -> %llds) - Reboot acked but ignored\n",
               before, after);
    return 1;
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL, *to = "cycle";
    int wantEp = 0, i, waitS = 20;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-boot - reboot an endpoint between main app and bootloader (pure C)\n"
                   "  --to bootloader|main|cycle   default 'cycle': app -> bootloader -> app\n"
                   "  --ip <addr> / --ep <index>   target endpoint\n"
                   "  --wait <s>                   reappear timeout (default 20)\n"
                   "  NON-DESTRUCTIVE: only reboots, never writes flash.\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")   && i+1 < argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")   && i+1 < argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--to")   && i+1 < argc) to = argv[++i];
        else if (!strcmp(argv[i], "--wait") && i+1 < argc) waitS = atoi(argv[++i]);
    }

    if (tool_select(wantIp, wantEp, 5, "LAN866x boot tool (pure C)") < 0) return 2;
    show_status("current");

    if (!strcmp(to, "bootloader")) {
        if (!reboot_to(RCP_IMAGE_BOOTLOADER, "bootloader", waitS)) return 3;
        show_status("bootloader");
        printf("\nEndpoint is now in the BOOTLOADER. Re-run with --to main to return.\n");
    } else if (!strcmp(to, "main")) {
        if (!reboot_to(RCP_IMAGE_MAIN, "main app", waitS)) return 3;
        show_status("main app");
    } else { /* cycle */
        if (!reboot_to(RCP_IMAGE_BOOTLOADER, "bootloader", waitS)) return 3;
        show_status("bootloader");
        if (!reboot_to(RCP_IMAGE_MAIN, "main app", waitS)) return 3;
        show_status("main app (restored)");
    }
    return 0;
}
