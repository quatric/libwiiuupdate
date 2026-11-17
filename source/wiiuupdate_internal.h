/* Shared between the library's source files; not part of the public API. */
#ifndef WIIUUPDATE_INTERNAL_H
#define WIIUUPDATE_INTERNAL_H

#include <stddef.h>
#include "wiiuupdate.h"

/* Receives body bytes as they arrive; return <0 to abort the transfer. */
typedef int (*wiiuupdate_sink)(void *ctx, const unsigned char *data, size_t len);

/* HTTPS GET, following up to 5 redirects. on_length (may be NULL) is told the
 * body size (-1 if unknown) before the first sink call. Returns 0 when the
 * whole body of a 200 reply was delivered. */
int wiiuupdate_http_get(const char *url, const char *accept, wiiuupdate_sink sink, void (*on_length)(void *, long long),
                        void *ctx, volatile int *cancel);

/* Highest numbered non-prerelease tag in a /releases reply. */
int wiiuupdate_newest_tag(const char *json, char *out, size_t out_len);

/* Download URL of the asset to install for release `tag`: a Wii U zip, else
 * a .wuhb, else an .rpx. Assets named for other consoles are skipped. */
extern const char *wiiuupdate_asset_name;
int wiiuupdate_release_asset(const char *json, const char *tag, char *out, size_t out_len);

/* Unpack a zip into app_dir. Entries under ".../apps/<Name>/" land in app_dir;
 * anything outside that folder is skipped (a zip without an apps/ folder is
 * unpacked as is). When exe_name is set (e.g. "Renamed.wuhb") and the folder
 * holds exactly one file with that extension, it is written under that name,
 * so a renamed copy is updated in place. Every file is written to a temporary
 * name first and only renamed over the old one once all of them were written,
 * so a failure leaves the installed app untouched. progress gets bytes written
 * so far and the total. Returns 0 on success; err gets a short reason. */
int wiiuupdate_extract(const char *zip_path, const char *app_dir, const char *exe_name,
                       void (*progress)(void *, unsigned long long, unsigned long long), void *ctx, char *err,
                       size_t err_len);

/* The "apps/<Name>" folder name inside a zip, or "" if none. */
int wiiuupdate_zip_app_name(const char *zip_path, char *out, size_t out_len);

/* The quit check set with WIIUUPDATE_SetQuitCheck: non-zero when the app has
 * been asked to quit. */
int wiiuupdate_should_quit(void);

/* Move src over dst: rename, or copy when the old file cannot be removed
 * (a running .wuhb can stay open). Returns 0 on success. */
int wiiuupdate_replace(const char *src, const char *dst);

/* ── Platform layer (wiiuupdate_sys.c; tests/host_sys.c on a desktop) ───── */

/* Run fn on a new background thread, below the app's own priority so a check
 * never takes time from its frame loop. A previous thread that has finished
 * is reaped first. Returns 0 when it started. */
int wiiuupdate_sys_thread(void *(*fn)(void *));
/* Wait for the thread to end; only call it once it has set its done flag. */
void wiiuupdate_sys_thread_join(void);
unsigned long long wiiuupdate_sys_ms(void);
void wiiuupdate_sys_sleep_ms(int ms);
/* Connect to the network if needed. Returns 0 when it is usable. */
int wiiuupdate_sys_net_up(volatile int *cancel);
/* Release whatever wiiuupdate_sys_net_up took for the library itself. */
void wiiuupdate_sys_net_down(void);
/* mbedTLS entropy source. */
int wiiuupdate_sys_entropy(void *data, unsigned char *out, size_t len, size_t *olen);
/* Path of the running executable when the system knows it (in any form
 * WIIUUPDATE_SetAppPath takes), else "". */
void wiiuupdate_sys_app_path(char *out, size_t len);
/* Let go of path if the system holds it open (the running .wuhb). Returns 0
 * when it did something worth retrying for. */
int wiiuupdate_sys_release_file(const char *path);
/* The SD card's root with a trailing slash: "fs:/vol/external01/". */
const char *wiiuupdate_sys_sd_root(void);

#endif
