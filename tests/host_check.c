/* End-to-end check against the real GitHub API from a desktop, using the
 * library's own TLS configuration and root certificates:
 *   make -f tests/Makefile.host build/host_check
 *   ./build/host_check quatric PictoChat-Client 1.0.0
 * With a fourth argument (the app's path on the SD card, which is build/sd here)
 * it also installs the update there:
 *   ./build/host_check quatric PictoChat-Client 1.0.0 wiiu/apps/PictoChat/PictoChat.wuhb */
#include <stdio.h>
#include <unistd.h>
#include "wiiuupdate.h"

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s owner repo version [app_path]\n", argv[0]); return 2; }
    if (argc > 4) WIIUUPDATE_SetAppPath(argv[4]);
    WIIUUPDATE_Start(argv[1], argv[2], argv[3]);
    wiiuupdate_state st = WIIUUPDATE_Wait(30000);
    static const char *names[] = { "idle", "checking", "up to date", "available", "failed", "cancelled" };
    printf("%s: %s latest=%s installable=%d\n%s\n", WIIUUPDATE_Url(), names[st], WIIUUPDATE_Latest(),
           WIIUUPDATE_CanInstall(), WIIUUPDATE_Message());
    if (argc > 4 && WIIUUPDATE_CanInstall()) {
        if (WIIUUPDATE_Install() < 0) { printf("install did not start\n"); return 1; }
        int last = -1;
        wiiuupdate_install_state ist;
        while ((ist = WIIUUPDATE_InstallState()) == WIIUUPDATE_INSTALL_DOWNLOADING || ist == WIIUUPDATE_INSTALL_EXTRACTING) {
            if (WIIUUPDATE_Progress() / 10 != last) printf("  %d%%\n", (last = WIIUUPDATE_Progress() / 10) * 10);
            usleep(50000);
        }
        WIIUUPDATE_Wait(5000);
        printf("install: %s %s -> %s\n", ist == WIIUUPDATE_INSTALL_DONE ? "done" : "failed", WIIUUPDATE_InstallError(),
               WIIUUPDATE_InstallDir());
        return ist != WIIUUPDATE_INSTALL_DONE;
    }
    return st == WIIUUPDATE_FAILED;
}
