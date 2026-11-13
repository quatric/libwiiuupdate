/* Text-mode helper on libwhb's log console: the app has called
 * WHBLogConsoleInit, this prints lines and redraws it while it waits. */
#include <stdio.h>
#include <vpad/input.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include "wiiuupdate_internal.h"

#define CHECK_LIMIT_MS  20000
#define NOTICE_MS       20000
#define FRAME_MS        16

/* The GamePad's A and B (apps with their own input library should pass a
 * wrapper around it as buttons_down). */
static uint32_t default_buttons(void)
{
    VPADStatus st;
    VPADReadError err;
    if (VPADRead(VPAD_CHAN_0, &st, 1, &err) < 1 || err != VPAD_READ_SUCCESS) return 0;
    return st.trigger;
}

static int elapsed_ms(unsigned long long since)
{
    return (int)(wiiuupdate_sys_ms() - since);
}

static void frame(void)
{
    WHBLogConsoleDraw();
    wiiuupdate_sys_sleep_ms(FRAME_MS);
}

/* Returns 0 when the app was asked to quit instead of A being pressed. */
static int wait_for_a(uint32_t (*buttons_down)(void))
{
    while (!(buttons_down() & VPAD_BUTTON_A)) {
        if (wiiuupdate_should_quit()) return 0;
        frame();
    }
    return 1;
}

/* Download and unpack with a progress line; B cancels while downloading. */
static wiiuupdate_state install_console(uint32_t (*buttons_down)(void))
{
    if (WIIUUPDATE_Install() < 0) {
        WHBLogPrint("  Could not start the update.");
        WHBLogConsoleDraw();
        return WIIUUPDATE_AVAILABLE;
    }
    wiiuupdate_install_state ist;
    int shown = -1;
    while ((ist = WIIUUPDATE_InstallState()) == WIIUUPDATE_INSTALL_DOWNLOADING || ist == WIIUUPDATE_INSTALL_EXTRACTING) {
        frame();
        int pct = WIIUUPDATE_Progress();
        if (pct / 10 != shown) { /* the log console is line based: one line per ten percent */
            WHBLogPrintf("  %s %3d%%", ist == WIIUUPDATE_INSTALL_DOWNLOADING ? "Downloading" : "Installing ", pct);
            shown = pct / 10;
        }
        if (wiiuupdate_should_quit() || (ist == WIIUUPDATE_INSTALL_DOWNLOADING && (buttons_down() & VPAD_BUTTON_B)))
            WIIUUPDATE_Cancel();
        if (wiiuupdate_should_quit()) {
            WIIUUPDATE_Wait(5000);
            return WIIUUPDATE_CANCELLED;
        }
    }
    WIIUUPDATE_Wait(5000);
    if (ist == WIIUUPDATE_INSTALL_DONE) {
        WHBLogPrintf("Updated to %s. Press A to return to the loader.", WIIUUPDATE_Latest());
        WHBLogConsoleDraw();
        return wait_for_a(buttons_down) ? WIIUUPDATE_UPDATED : WIIUUPDATE_CANCELLED;
    }
    WHBLogPrintf("Update failed: %s. Press A to continue.", WIIUUPDATE_InstallError());
    WHBLogConsoleDraw();
    return wait_for_a(buttons_down) ? WIIUUPDATE_AVAILABLE : WIIUUPDATE_CANCELLED;
}

wiiuupdate_state WIIUUPDATE_CheckConsole(const char *owner, const char *repo, const char *current_version,
                                         uint32_t (*buttons_down)(void))
{
    if (!buttons_down) buttons_down = default_buttons;

    WHBLogPrint("Checking for updates (B to skip)...");
    WHBLogConsoleDraw();
    if (WIIUUPDATE_Start(owner, repo, current_version) < 0) {
        WHBLogPrint("Could not check for updates.");
        WHBLogConsoleDraw();
        return WIIUUPDATE_FAILED;
    }

    unsigned long long start = wiiuupdate_sys_ms();
    while (WIIUUPDATE_State() == WIIUUPDATE_CHECKING) {
        frame();
        int skip = (buttons_down() & VPAD_BUTTON_B) != 0;
        if (skip || wiiuupdate_should_quit() || elapsed_ms(start) > CHECK_LIMIT_MS) {
            WIIUUPDATE_Cancel();
            WHBLogPrint(skip ? "Update check skipped." : "Update check timed out.");
            WHBLogConsoleDraw();
            return WIIUUPDATE_CANCELLED;
        }
    }

    wiiuupdate_state st = WIIUUPDATE_State();
    if (st == WIIUUPDATE_UP_TO_DATE) {
        WHBLogPrint("Up to date.");
    } else if (st == WIIUUPDATE_AVAILABLE) {
        WIIUUPDATE_TakeNotice();
        int can = WIIUUPDATE_CanInstall();
        WHBLogPrint(WIIUUPDATE_Message());
        if (can) WHBLogPrint("A: Update now    B: Later");
        else WHBLogPrintf("Download it from %s. Press A to continue.", WIIUUPDATE_Url());
        WHBLogConsoleDraw();
        uint32_t pressed = 0;
        start = wiiuupdate_sys_ms();
        while (elapsed_ms(start) < NOTICE_MS && !wiiuupdate_should_quit()) {
            frame();
            if ((pressed = buttons_down() & (VPAD_BUTTON_A | VPAD_BUTTON_B))) break;
        }
        if (wiiuupdate_should_quit()) return WIIUUPDATE_CANCELLED;
        if (can && (pressed & VPAD_BUTTON_A)) return install_console(buttons_down);
    } else {
        WHBLogPrint("Could not check for updates.");
    }
    WHBLogConsoleDraw();
    return st;
}
