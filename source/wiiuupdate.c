#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include "wiiuupdate_internal.h"

#define MAX_BODY    (512 * 1024)
#define DOWNLOAD_SHARE 80 /* percent of the progress bar spent downloading */

static volatile wiiuupdate_state state = WIIUUPDATE_IDLE;
static volatile wiiuupdate_install_state install_state = WIIUUPDATE_INSTALL_IDLE;
static volatile int cancelled;
static volatile int noticed;
static volatile int running; /* a thread is alive, even after a cancel */
static volatile int progress;
static char owner[64], repo[96], current[48], latest[48], url[200], message[200];
static char asset[1024], app_path[256], install_dir[384], install_error[160];

static int start_thread(void *(*fn)(void *))
{
    running = 1;
    if (wiiuupdate_sys_thread(fn) != 0) {
        running = 0;
        return -1;
    }
    return 0;
}

/* ── Check ───────────────────────────────────────────────────────────────── */

typedef struct {
    char *data;
    size_t len;
} membuf_t;

static int mem_sink(void *ctx, const unsigned char *data, size_t len)
{
    membuf_t *m = ctx;
    if (m->len + len > MAX_BODY) len = MAX_BODY - m->len; /* keep what fits */
    memcpy(m->data + m->len, data, len);
    m->len += len;
    return 0;
}

static void *check_thread(void *arg)
{
    (void)arg;
    wiiuupdate_state result = WIIUUPDATE_FAILED;
    char tag[sizeof latest];
    membuf_t body = { malloc(MAX_BODY + 1), 0 };

    if (body.data && wiiuupdate_sys_net_up(&cancelled) == 0 && !cancelled) {
        char api[256];
        snprintf(api, sizeof api, "https://api.github.com/repos/%s/%s/releases?per_page=10", owner, repo);
        if (wiiuupdate_http_get(api, "application/vnd.github+json", mem_sink, NULL, &body, &cancelled) == 0) {
            body.data[body.len] = 0;
            if (wiiuupdate_newest_tag(body.data, tag, sizeof tag) == 0 && WIIUUPDATE_CompareVersions(tag, current) > 0) {
                snprintf(latest, sizeof latest, "%s", tag);
                snprintf(message, sizeof message, "Version %s is available (you have %s%s).", latest,
                         isdigit((unsigned char)current[0]) ? "v" : "", current);
                if (wiiuupdate_release_asset(body.data, tag, asset, sizeof asset) < 0) asset[0] = 0;
                result = WIIUUPDATE_AVAILABLE;
            } else {
                result = WIIUUPDATE_UP_TO_DATE;
            }
        }
    }
    wiiuupdate_sys_net_down();
    free(body.data);
    state = cancelled ? WIIUUPDATE_CANCELLED : result;
    running = 0;
    return NULL;
}

int WIIUUPDATE_Start(const char *o, const char *r, const char *version)
{
    if (running) return -1;
    snprintf(owner, sizeof owner, "%s", o);
    snprintf(repo, sizeof repo, "%s", r);
    snprintf(current, sizeof current, "%s", version ? version : "0");
    snprintf(url, sizeof url, "github.com/%s/%s/releases", owner, repo);
    latest[0] = message[0] = asset[0] = 0;
    cancelled = noticed = 0;
    install_state = WIIUUPDATE_INSTALL_IDLE;
    state = WIIUUPDATE_CHECKING;
    if (start_thread(check_thread) < 0) {
        state = WIIUUPDATE_FAILED;
        return -1;
    }
    return 0;
}

/* ── Install ─────────────────────────────────────────────────────────────── */

typedef struct {
    FILE *f;
    long long total, done;
} download_t;

static int file_sink(void *ctx, const unsigned char *data, size_t len)
{
    download_t *d = ctx;
    if (fwrite(data, 1, len, d->f) != len) return -1;
    d->done += len;
    if (d->total > 0) progress = (int)(d->done * DOWNLOAD_SHARE / d->total);
    return 0;
}

static void on_length(void *ctx, long long len) { ((download_t *)ctx)->total = len; }

static void on_extract(void *ctx, unsigned long long done, unsigned long long total)
{
    (void)ctx;
    if (total) progress = DOWNLOAD_SHARE + (int)(done * (100 - DOWNLOAD_SHARE) / total);
}

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int has_ext(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);
    return n > e && !strcasecmp(name + n - e, ext);
}

static int fail(const char *why)
{
    snprintf(install_error, sizeof install_error, "%s", why);
    return -1;
}

static int install(void)
{
    char *tmp = malloc(320);
    char root[256], exe[96] = "";
    int result = -1;
    if (!tmp) return fail("out of memory");

    if (!app_path[0]) {
        char sys_path[256];
        wiiuupdate_sys_app_path(sys_path, sizeof sys_path);
        WIIUUPDATE_SetAppPath(sys_path);
    }
    if (app_path[0]) {
        /* The running app's own file: install into its folder. */
        snprintf(install_dir, sizeof install_dir, "%s", app_path);
        char *slash = strrchr(install_dir, '/');
        snprintf(exe, sizeof exe, "%s", slash + 1);
        *slash = 0;
        snprintf(root, sizeof root, "%s", install_dir);
    } else {
        install_dir[0] = 0;
        snprintf(root, sizeof root, "%swiiu", wiiuupdate_sys_sd_root());
        mkdir(root, 0777);
        snprintf(root, sizeof root, "%swiiu/apps", wiiuupdate_sys_sd_root());
        mkdir(root, 0777);
    }
    if (!is_dir(root)) {
        free(tmp);
        return fail(install_dir[0] ? "cannot find the app's folder" : "no SD card found");
    }

    int is_zip = has_ext(asset, ".zip");
    snprintf(tmp, 320, "%s/wiiuupdate-download.tmp", root);
    download_t d = { fopen(tmp, "wb"), -1, 0 };
    if (!d.f) {
        free(tmp);
        return fail("cannot write to the SD card");
    }
    install_state = WIIUUPDATE_INSTALL_DOWNLOADING;
    int ok = wiiuupdate_sys_net_up(&cancelled) == 0 &&
             wiiuupdate_http_get(asset, "application/octet-stream", file_sink, on_length, &d, &cancelled) == 0;
    if (fclose(d.f) != 0) ok = 0;
    wiiuupdate_sys_net_down();
    if (!ok) {
        fail(cancelled ? "cancelled" : "download failed");
        goto out;
    }
    progress = DOWNLOAD_SHARE;
    install_state = WIIUUPDATE_INSTALL_EXTRACTING;

    if (!install_dir[0]) {
        char name[96];
        if (!is_zip || wiiuupdate_zip_app_name(tmp, name, sizeof name) < 0) snprintf(name, sizeof name, "%s", repo);
        snprintf(install_dir, sizeof install_dir, "%s/%s", root, name);
    }

    if (is_zip) {
        if (wiiuupdate_extract(tmp, install_dir, exe[0] ? exe : NULL, on_extract, NULL, install_error,
                               sizeof install_error) < 0)
            goto out;
    } else {
        /* A bare .wuhb or .rpx replaces the running file when it is the same kind. */
        const char *ext = has_ext(asset, ".wuhb") ? ".wuhb" : ".rpx";
        char target[512];
        mkdir(install_dir, 0777);
        if (exe[0] && has_ext(exe, ext)) snprintf(target, sizeof target, "%s/%s", install_dir, exe);
        else snprintf(target, sizeof target, "%s/%s%s", install_dir, repo, ext);
        if (wiiuupdate_replace(tmp, target) != 0) {
            fail("could not replace the app");
            goto out;
        }
    }
    progress = 100;
    result = 0;

out:
    remove(tmp);
    free(tmp);
    return result;
}

static void *install_thread(void *arg)
{
    (void)arg;
    install_state = install() == 0 ? WIIUUPDATE_INSTALL_DONE : WIIUUPDATE_INSTALL_FAILED;
    running = 0;
    return NULL;
}

void WIIUUPDATE_SetAppPath(const char *path)
{
    app_path[0] = 0;
    if (!path || !strchr(path, '/') || path[strlen(path) - 1] == '/') return;
    /* Everything ends up as "fs:/vol/external01/..."; other devices are not ours to write. */
    const char *sd = wiiuupdate_sys_sd_root();
    if (!strncmp(path, "fs:/vol/external01/", 19)) snprintf(app_path, sizeof app_path, "%s%s", sd, path + 19);
    else if (!strncmp(path, "/vol/external01/", 16)) snprintf(app_path, sizeof app_path, "%s%s", sd, path + 16);
    else if (!strncmp(path, "sd:/", 4)) snprintf(app_path, sizeof app_path, "%s%s", sd, path + 4);
    else if (path[0] != '/' && !strchr(path, ':')) snprintf(app_path, sizeof app_path, "%s%s", sd, path);
}

int WIIUUPDATE_CanInstall(void) { return state == WIIUUPDATE_AVAILABLE && asset[0]; }

int WIIUUPDATE_Install(void)
{
    WIIUUPDATE_Wait(2000); /* the check thread may still be returning */
    if (running || !WIIUUPDATE_CanInstall()) return -1;
    if (install_state == WIIUUPDATE_INSTALL_DOWNLOADING || install_state == WIIUUPDATE_INSTALL_EXTRACTING) return -1;
    cancelled = 0;
    progress = 0;
    install_error[0] = 0;
    install_state = WIIUUPDATE_INSTALL_DOWNLOADING;
    if (start_thread(install_thread) < 0) {
        install_state = WIIUUPDATE_INSTALL_FAILED;
        fail("could not start");
        return -1;
    }
    return 0;
}

wiiuupdate_install_state WIIUUPDATE_InstallState(void) { return install_state; }
int WIIUUPDATE_Progress(void) { return progress; }
const char *WIIUUPDATE_InstallError(void) { return install_error; }
const char *WIIUUPDATE_InstallDir(void) { return install_dir; }

/* ── Shared ──────────────────────────────────────────────────────────────── */

wiiuupdate_state WIIUUPDATE_State(void) { return state; }

wiiuupdate_state WIIUUPDATE_Wait(int timeout_ms)
{
    unsigned long long start = wiiuupdate_sys_ms();
    while (running) { /* also covers a cancelled check whose thread is still winding down */
        if (timeout_ms >= 0 && wiiuupdate_sys_ms() - start >= (unsigned long long)timeout_ms) break;
        wiiuupdate_sys_sleep_ms(10);
    }
    return state;
}

void WIIUUPDATE_Cancel(void)
{
    cancelled = 1;
    if (install_state == WIIUUPDATE_INSTALL_IDLE &&
        (state == WIIUUPDATE_CHECKING || state == WIIUUPDATE_AVAILABLE))
        state = WIIUUPDATE_CANCELLED;
}

int WIIUUPDATE_TakeNotice(void)
{
    if (state != WIIUUPDATE_AVAILABLE || noticed) return 0;
    noticed = 1;
    return 1;
}

const char *WIIUUPDATE_Latest(void) { return latest; }
const char *WIIUUPDATE_Current(void) { return current; }
const char *WIIUUPDATE_Url(void) { return url; }
const char *WIIUUPDATE_Message(void) { return message; }
