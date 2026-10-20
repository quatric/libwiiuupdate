/* libwiiuupdate example: checks quatric/PictoChat-Client for a newer release
 * than 0.0.1 and offers to install it, on the console. */
#include <stdio.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <vpad/input.h>
#include <whb/proc.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include <wiiuupdate.h>

#define OWNER   "quatric"
#define REPO    "PictoChat-Client"
#define VERSION "0.0.1"

int main(void)
{
    WHBProcInit();
    WHBLogConsoleInit();
    WHBLogPrintf("libwiiuupdate example: %s/%s, running %s", OWNER, REPO, VERSION);
    WHBLogConsoleDraw();
    WIIUUPDATE_Start(OWNER, REPO, VERSION);

    wiiuupdate_popup pop;
    int last_state = -1, last_install = -1, last_progress = -1, done = 0;
    while (WHBProcIsRunning()) {
        VPADStatus vpad;
        VPADReadError err;
        uint32_t down = VPADRead(VPAD_CHAN_0, &vpad, 1, &err) == 1 && err == VPAD_READ_SUCCESS ? vpad.trigger : 0;

        if (!done && WIIUUPDATE_State() != last_state) {
            last_state = WIIUUPDATE_State();
            static const char *names[] = { "idle", "checking", "up to date", "update available", "could not check",
                                           "cancelled" };
            WHBLogPrintf("check: %s", names[last_state]);
        }
        int st = WIIUUPDATE_Popup(down, &pop);
        if (st == WIIUUPDATE_POPUP_OPEN &&
            ((int)WIIUUPDATE_InstallState() != last_install || pop.progress / 10 != last_progress / 10)) {
            last_install = WIIUUPDATE_InstallState();
            last_progress = pop.progress;
            WHBLogPrintf("%s: %s %s", pop.title, pop.text, pop.detail);
            if (pop.progress >= 0) WHBLogPrintf("  %d%%", pop.progress);
            if (pop.hint[0]) WHBLogPrintf("  %s", pop.hint);
        }
        if (st == WIIUUPDATE_POPUP_EXIT && !done) {
            done = 1;
            WHBLogPrintf("Installed to %s. Press HOME to leave.", WIIUUPDATE_InstallDir());
        }
        WHBLogConsoleDraw();
        OSSleepTicks(OSMillisecondsToTicks(16));
    }

    WIIUUPDATE_Cancel();
    WIIUUPDATE_Wait(5000);
    WHBLogConsoleFree();
    WHBProcShutdown();
    return 0;
}
