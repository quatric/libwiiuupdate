/* The Wii U side: threads, time, the network connection, entropy, and what
 * the Aroma RPX loader knows about the running app. */
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/dynload.h>
#include <nn/ac.h>
#include <mbedtls/platform_time.h>
#include "wiiuupdate_internal.h"

#define STACK_SIZE (128 * 1024)

/* Cafe OS priorities run 0 (first) to 31 and apps start their main thread at
 * 16: below it, the check only runs while the app is idle or waiting for vsync. */
#define THREAD_PRIORITY 24

static pthread_t last_thread;
static int have_thread;
static void *(*thread_fn)(void *);

static void *thread_main(void *arg)
{
    OSSetThreadPriority(OSGetCurrentThread(), THREAD_PRIORITY);
    return thread_fn(arg);
}

void wiiuupdate_sys_thread_join(void)
{
    if (!have_thread) return;
    pthread_join(last_thread, NULL);
    have_thread = 0;
}

int wiiuupdate_sys_thread(void *(*fn)(void *))
{
    wiiuupdate_sys_thread_join(); /* the last thread has set its flag and is returning */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, STACK_SIZE);
    thread_fn = fn;
    int ret = pthread_create(&last_thread, &attr, thread_main, NULL);
    pthread_attr_destroy(&attr);
    if (ret == 0) have_thread = 1;
    return ret;
}

unsigned long long wiiuupdate_sys_ms(void) { return (unsigned long long)OSTicksToMilliseconds(OSGetTime()); }
void wiiuupdate_sys_sleep_ms(int ms) { usleep(ms * 1000); }

mbedtls_ms_time_t mbedtls_ms_time(void) { return (mbedtls_ms_time_t)wiiuupdate_sys_ms(); }

int wiiuupdate_sys_net_up(volatile int *cancel)
{
    static int ac_ready;
    if (!ac_ready) {
        if (NNResult_IsFailure(ACInitialize())) return -1;
        ac_ready = 1; /* kept: the app may use nn::ac too */
    }
    BOOL connected = FALSE;
    ACIsApplicationConnected(&connected);
    for (int attempt = 0; attempt < 3 && !connected && !*cancel; attempt++)
        if (NNResult_IsSuccess(ACConnect())) connected = TRUE;
    return connected ? 0 : -1;
}

void wiiuupdate_sys_net_down(void) {}

/* Homebrew has no hardware RNG to call; gather timing jitter from the CPU
 * timebase while doing memory work, which varies with bus and cache state. */
int wiiuupdate_sys_entropy(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void)data;
    static volatile uint32_t scratch[256];
    for (size_t i = 0; i < len; i++) {
        uint32_t acc = 0;
        for (int k = 0; k < 16; k++) {
            uint64_t t0 = (uint64_t)OSGetTime();
            for (int n = 0; n < (int)(t0 & 31) + 8; n++) scratch[(t0 + n * 37) & 255] += (uint32_t)t0 ^ n;
            uint64_t t1 = (uint64_t)OSGetTime();
            acc = (acc << 3 | acc >> 29) ^ (uint32_t)(t1 - t0) ^ (uint32_t)t1;
        }
        out[i] = (unsigned char)(acc ^ acc >> 8 ^ acc >> 16 ^ acc >> 24);
    }
    *olen = len;
    return 0;
}

/* The Aroma RPX loader's exports, looked up at run time so the library needs
 * no extra package and still works without Aroma. */
static void *rpxloader(const char *name)
{
    OSDynLoad_Module module;
    void *fn = NULL;
    if (OSDynLoad_Acquire("homebrew_rpx_loader", &module) != OS_DYNLOAD_OK) return NULL;
    if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, name, &fn) != OS_DYNLOAD_OK) fn = NULL;
    return fn;
}

void wiiuupdate_sys_app_path(char *out, size_t len)
{
    /* e.g. "wiiu/apps/MyApp/MyApp.wuhb", relative to the SD card */
    int (*get_path)(char *, uint32_t) = (int (*)(char *, uint32_t))rpxloader("RL_GetPathOfRunningExecutable");
    memset(out, 0, len);
    if (!get_path || get_path(out, len) != 0) out[0] = 0;
}

int wiiuupdate_sys_release_file(const char *path)
{
    /* The running .wuhb stays open while it is mounted as /vol/content. */
    char rel[256] = "";
    int (*get_path)(char *, uint32_t) = (int (*)(char *, uint32_t))rpxloader("RL_GetPathOfRunningExecutable");
    if (!get_path || get_path(rel, sizeof rel) != 0 || !rel[0]) return -1;
    size_t n = strlen(path), r = strlen(rel);
    if (n < r || strcmp(path + n - r, rel)) return -1;
    int (*unmount)(void) = (int (*)(void))rpxloader("RL_UnmountCurrentRunningBundle");
    return unmount && unmount() == 0 ? 0 : -1;
}

const char *wiiuupdate_sys_sd_root(void) { return "fs:/vol/external01/"; }
