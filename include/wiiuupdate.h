/* libwiiuupdate: check GitHub for a newer release of a Wii U homebrew app
 * (Aroma, .wuhb), and optionally install it over the running copy.
 *
 * The check runs on its own thread (network bring-up, HTTPS request to the
 * GitHub releases API, version comparison) so it never blocks the app:
 *
 *     WIIUUPDATE_Start("quatric", "MyApp", "1.2.0");
 *     ...each frame...
 *     if (WIIUUPDATE_TakeNotice()) show_popup(WIIUUPDATE_Message());
 *
 * Failures are silent: no network or no reply just means no notice.
 *
 * Installing: WIIUUPDATE_Install() and poll WIIUUPDATE_InstallState() /
 * WIIUUPDATE_Progress(). The release's zip (or .wuhb) is streamed to the SD
 * card, then unpacked over the app's folder. Once it reports
 * WIIUUPDATE_INSTALL_DONE the app should exit back to the Wii U Menu.
 */
#ifndef WIIUUPDATE_H
#define WIIUUPDATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIIUUPDATE_IDLE = 0,     /* WIIUUPDATE_Start not called yet */
    WIIUUPDATE_CHECKING,     /* thread running */
    WIIUUPDATE_UP_TO_DATE,   /* no release newer than the running version */
    WIIUUPDATE_AVAILABLE,    /* WIIUUPDATE_Latest() is newer */
    WIIUUPDATE_FAILED,       /* no network, TLS or HTTP error, bad reply */
    WIIUUPDATE_CANCELLED,    /* WIIUUPDATE_Cancel was called */
} wiiuupdate_state;

typedef enum {
    WIIUUPDATE_INSTALL_IDLE = 0,
    WIIUUPDATE_INSTALL_DOWNLOADING,
    WIIUUPDATE_INSTALL_EXTRACTING,
    WIIUUPDATE_INSTALL_DONE,     /* new files are in place */
    WIIUUPDATE_INSTALL_FAILED,   /* see WIIUUPDATE_InstallError(); the old copy is untouched */
} wiiuupdate_install_state;

/* Start a background check of github.com/<owner>/<repo>/releases against
 * current_version (e.g. "1.2.0" or "v1.2.0"). Connects to the network if the
 * app has not. Returns 0 when the thread started. Strings are copied. */
int WIIUUPDATE_Start(const char *owner, const char *repo, const char *current_version);

wiiuupdate_state WIIUUPDATE_State(void);

/* Block until the check (or install) thread has finished, or timeout_ms
 * passes (<0 waits forever). */
wiiuupdate_state WIIUUPDATE_Wait(int timeout_ms);

/* Stop caring about the result. The thread finishes in the background; its
 * result is discarded and the state becomes WIIUUPDATE_CANCELLED. During an
 * install this stops the download. */
void WIIUUPDATE_Cancel(void);

/* Returns 1 exactly once, the first time it is called after a newer release
 * was found. Meant for a GUI popup: poll it every frame. */
int WIIUUPDATE_TakeNotice(void);

/* Newest release tag found, e.g. "v1.3.0" ("" until known). */
const char *WIIUUPDATE_Latest(void);
/* The running version as passed to WIIUUPDATE_Start. */
const char *WIIUUPDATE_Current(void);
/* "github.com/<owner>/<repo>/releases" */
const char *WIIUUPDATE_Url(void);
/* A short line for popups, e.g. "Version v1.3.0 is available (you have v1.2.0)." */
const char *WIIUUPDATE_Message(void);

/* Where the running app lives, e.g. "fs:/vol/external01/wiiu/apps/MyApp/MyApp.wuhb".
 * Optional: under Aroma the library asks the RPX loader itself. Without
 * either, updates go to wiiu/apps/<folder named in the zip>/ on the SD card. */
void WIIUUPDATE_SetAppPath(const char *path);

/* 1 when an update was found and its release has an installable asset (a
 * Wii U zip, or a .wuhb / .rpx). */
int WIIUUPDATE_CanInstall(void);

/* Download and install the update on a background thread. WIIUUPDATE_Cancel
 * stops the download; unpacking is not interrupted. Returns 0 if started. */
int WIIUUPDATE_Install(void);

wiiuupdate_install_state WIIUUPDATE_InstallState(void);
/* 0-100 over download and unpacking. */
int WIIUUPDATE_Progress(void);
/* Why the install failed, e.g. "download failed" ("" otherwise). */
const char *WIIUUPDATE_InstallError(void);
/* The folder the update was installed to. */
const char *WIIUUPDATE_InstallDir(void);

/* Ready-made popup logic for GUI apps. Call once per frame with the
 * VPAD_BUTTON_* bits pressed this frame (map other controllers onto them);
 * while it returns OPEN, draw a box from *popup and keep the buttons away from
 * the app. A installs (or closes the popup when there is nothing to install),
 * B closes it or cancels the download. EXIT means the update is installed and
 * the user pressed A: leave the app the way it normally exits. */
enum { WIIUUPDATE_POPUP_CLOSED = 0, WIIUUPDATE_POPUP_OPEN, WIIUUPDATE_POPUP_EXIT };
typedef struct {
    const char *title;   /* "Update available", "Updating...", "Updated", "Update failed" */
    char text[160];      /* e.g. "Version v1.3.0 is available (you have v1.2.0)." */
    const char *detail;  /* a second line (the releases URL) or "" */
    const char *hint;    /* buttons, e.g. "A: Update now    B: Later", or "" */
    int progress;        /* 0-100 while updating (draw a bar), else -1 */
} wiiuupdate_popup;
int WIIUUPDATE_Popup(uint32_t buttons_down, wiiuupdate_popup *popup);

/* Compare two version strings numerically ("v1.10" > "1.9.2"). Leading text
 * before the first digit is skipped. Returns <0, 0 or >0. */
int WIIUUPDATE_CompareVersions(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif
