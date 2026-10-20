/* The update popup's logic, shared by GUI apps: they only draw it. */
#include <stdio.h>
#include "wiiuupdate.h"

/* VPAD_BUTTON_A / VPAD_BUTTON_B from <vpad/input.h>, kept here so the popup
 * logic builds anywhere. */
#define BUTTON_A 0x8000
#define BUTTON_B 0x4000

static int open;

int WIIUUPDATE_Popup(uint32_t down, wiiuupdate_popup *p)
{
    if (WIIUUPDATE_TakeNotice()) open = 1;
    if (!open) return WIIUUPDATE_POPUP_CLOSED;

    int a = (down & BUTTON_A) != 0, b = (down & BUTTON_B) != 0;
    int result = WIIUUPDATE_POPUP_OPEN;
    switch (WIIUUPDATE_InstallState()) {
    case WIIUUPDATE_INSTALL_IDLE:
        if (a && WIIUUPDATE_CanInstall()) WIIUUPDATE_Install();
        else if (a || b) open = 0;
        break;
    case WIIUUPDATE_INSTALL_DOWNLOADING:
    case WIIUUPDATE_INSTALL_EXTRACTING:
        if (b) WIIUUPDATE_Cancel();
        break;
    case WIIUUPDATE_INSTALL_DONE:
        if (a) result = WIIUUPDATE_POPUP_EXIT;
        break;
    case WIIUUPDATE_INSTALL_FAILED:
        if (a || b) open = 0;
        break;
    }
    if (!open) return WIIUUPDATE_POPUP_CLOSED;

    /* Describe the state after this frame's input. */
    p->progress = -1;
    switch (WIIUUPDATE_InstallState()) {
    case WIIUUPDATE_INSTALL_IDLE:
        p->title = "Update available";
        snprintf(p->text, sizeof p->text, "%s", WIIUUPDATE_Message());
        p->hint = WIIUUPDATE_CanInstall() ? "A: Update now    B: Later" : "Press A to continue";
        p->detail = WIIUUPDATE_CanInstall() ? "" : WIIUUPDATE_Url();
        break;
    case WIIUUPDATE_INSTALL_DOWNLOADING:
    case WIIUUPDATE_INSTALL_EXTRACTING:
        p->title = "Updating...";
        snprintf(p->text, sizeof p->text, "%s %s",
                 WIIUUPDATE_InstallState() == WIIUUPDATE_INSTALL_DOWNLOADING ? "Downloading" : "Installing",
                 WIIUUPDATE_Latest());
        p->hint = WIIUUPDATE_InstallState() == WIIUUPDATE_INSTALL_DOWNLOADING ? "B: Cancel" : "";
        p->detail = "";
        p->progress = WIIUUPDATE_Progress();
        break;
    case WIIUUPDATE_INSTALL_DONE:
        p->title = "Updated";
        snprintf(p->text, sizeof p->text, "Version %s is installed.", WIIUUPDATE_Latest());
        p->hint = "Press A to return to the Wii U Menu";
        p->detail = "";
        break;
    case WIIUUPDATE_INSTALL_FAILED:
        p->title = "Update failed";
        snprintf(p->text, sizeof p->text, "Could not update: %s.", WIIUUPDATE_InstallError());
        p->hint = "Press A to continue";
        p->detail = "";
        break;
    }
    return result;
}
