/* Host-side test of version comparison, release parsing and zip unpacking:
 *   make -f tests/Makefile.host build/host_test && ./build/host_test [releases.json] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "miniz.h"
#include "wiiuupdate_internal.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static int file_is(const char *path, const char *want)
{
    char got[64] = "";
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fread(got, 1, sizeof got - 1, f);
    fclose(f);
    return !strcmp(got, want);
}

static void make_zip(const char *zip, const char *const *entries)
{
    mz_zip_archive zw;
    memset(&zw, 0, sizeof zw);
    CHECK(mz_zip_writer_init_file(&zw, zip, 0));
    for (; *entries; entries += 2)
        mz_zip_writer_add_mem(&zw, entries[0], entries[1], strlen(entries[1]), MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_finalize_archive(&zw);
    mz_zip_writer_end(&zw);
}

int main(int argc, char **argv)
{
    CHECK(WIIUUPDATE_CompareVersions("v1.10", "1.9.2") > 0);
    CHECK(WIIUUPDATE_CompareVersions("v1.0", "1.0.0") == 0);
    CHECK(WIIUUPDATE_CompareVersions("1.0", "v1.0.1") < 0);
    CHECK(WIIUUPDATE_CompareVersions("v2.0-beta", "2.0") == 0);

    char tag[48];
    const char *json = "[{\"tag_name\":\"latest\",\"prerelease\":false},"
                       "{\"tag_name\":\"v1.1.0\",\"draft\":false,\"prerelease\":false,\"body\":\"x \\\"tag_name\\\": \\\"v9\\\"\"},"
                       "{\"tag_name\":\"v2.0.0\",\"prerelease\":true},"
                       "{\"tag_name\":\"v1.0.0\",\"prerelease\":false}]";
    CHECK(wiiuupdate_newest_tag(json, tag, sizeof tag) == 0);
    CHECK(!strcmp(tag, "v1.1.0"));
    CHECK(wiiuupdate_newest_tag("[]", tag, sizeof tag) < 0);

    /* A release for every console: the Wii U zip wins, the Wii and 3DS ones never do. */
    char asset[256];
    const char *rel =
        "[{\"tag_name\":\"v1.3\",\"assets\":["
        "{\"browser_download_url\":\"https://x/quatric/App-Wii/releases/download/v1.3/App-3DS.zip\"},"
        "{\"browser_download_url\":\"https://x/quatric/App-Wii/releases/download/v1.3/App-Wii.zip\"},"
        "{\"browser_download_url\":\"https://x/quatric/App-Wii/releases/download/v1.3/App.rpx\"},"
        "{\"browser_download_url\":\"https://x/quatric/App-Wii/releases/download/v1.3/App.wuhb\"},"
        "{\"browser_download_url\":\"https://x/quatric/App-Wii/releases/download/v1.3/App-WUP.zip\"},"
        "{\"browser_download_url\":\"https://x/quatric/App-Wii/releases/download/v1.3/App.3dsx\"}],"
        "\"body\":\"\\\"browser_download_url\\\":\\\"https://evil/x-wiiu.zip\\\"\"},"
        "{\"tag_name\":\"v1.2\",\"assets\":[{\"browser_download_url\":\"https://x/App-Wii.zip\"},"
        "{\"browser_download_url\":\"https://x/App.wuhb\"}]},"
        "{\"tag_name\":\"v1.1\",\"assets\":[{\"browser_download_url\":\"https://x/App-WiiU.zip\"}]},"
        "{\"tag_name\":\"v1.0\",\"assets\":[{\"browser_download_url\":\"https://x/App.zip\"},"
        "{\"browser_download_url\":\"https://x/App.rpx\"}]},"
        "{\"tag_name\":\"v0.9\",\"assets\":[{\"browser_download_url\":\"https://x/App-Wii.zip\"},"
        "{\"browser_download_url\":\"https://x/App-3DS.zip\"},{\"browser_download_url\":\"https://x/App.dol\"}]}]";
    CHECK(wiiuupdate_release_asset(rel, "v1.3", asset, sizeof asset) == 0 && strstr(asset, "/App-WUP.zip"));
    CHECK(wiiuupdate_release_asset(rel, "v1.2", asset, sizeof asset) == 0 && !strcmp(asset, "https://x/App.wuhb"));
    CHECK(wiiuupdate_release_asset(rel, "v1.1", asset, sizeof asset) == 0 && !strcmp(asset, "https://x/App-WiiU.zip"));
    CHECK(wiiuupdate_release_asset(rel, "v1.0", asset, sizeof asset) == 0 && !strcmp(asset, "https://x/App.zip"));
    CHECK(wiiuupdate_release_asset(rel, "v0.9", asset, sizeof asset) < 0);
    CHECK(wiiuupdate_release_asset(rel, "v2", asset, sizeof asset) < 0);

    /* Unpack: only wiiu/apps/<Name>/ lands in the app folder, nothing escapes it. */
    const char *zip = "build/test.zip";
    static const char *const entries[] = {
        "README.md", "readme",
        "wiiu/apps/MyApp/MyApp.wuhb", "new wuhb",
        "wiiu/apps/MyApp/data/level1.bin", "level",
        "wiiu/apps/MyApp/../../escape.txt", "bad",
        NULL,
    };
    make_zip(zip, entries);
    char name[64], err[128];
    CHECK(wiiuupdate_zip_app_name(zip, name, sizeof name) == 0 && !strcmp(name, "MyApp"));
    system("rm -rf build/sd && mkdir -p build/sd/wiiu/apps/MyApp && echo old > build/sd/wiiu/apps/MyApp/MyApp.wuhb"
           " && echo keep > build/sd/wiiu/apps/MyApp/settings.txt");
    CHECK(wiiuupdate_extract(zip, "build/sd/wiiu/apps/MyApp", "MyApp.wuhb", NULL, NULL, err, sizeof err) == 0);
    CHECK(file_is("build/sd/wiiu/apps/MyApp/MyApp.wuhb", "new wuhb"));
    struct stat st;
    CHECK(stat("build/sd/wiiu/apps/MyApp/data/level1.bin", &st) == 0);
    CHECK(file_is("build/sd/wiiu/apps/MyApp/settings.txt", "keep\n"));
    CHECK(stat("build/sd/wiiu/apps/MyApp/README.md", &st) != 0);
    CHECK(stat("build/sd/escape.txt", &st) != 0 && stat("build/escape.txt", &st) != 0);
    CHECK(stat("build/sd/wiiu/apps/MyApp/MyApp.wuhb.wutmp", &st) != 0);
    CHECK(wiiuupdate_extract("build/missing.zip", "build/sd/wiiu/apps/MyApp", NULL, NULL, NULL, err, sizeof err) < 0);

    /* A copy that was renamed, or moved out of its folder, is updated under its own name. */
    system("rm -rf build/sd && mkdir -p build/sd/wiiu/apps && echo old > build/sd/wiiu/apps/Mine.wuhb");
    CHECK(wiiuupdate_extract(zip, "build/sd/wiiu/apps", "Mine.wuhb", NULL, NULL, err, sizeof err) == 0);
    CHECK(file_is("build/sd/wiiu/apps/Mine.wuhb", "new wuhb"));
    CHECK(stat("build/sd/wiiu/apps/MyApp.wuhb", &st) != 0);

    /* Two candidates: names are left alone. */
    static const char *const two[] = {
        "wiiu/apps/MyApp/MyApp.wuhb", "a", "wiiu/apps/MyApp/MyAppDebug.wuhb", "b", NULL,
    };
    make_zip("build/two.zip", two);
    system("rm -rf build/sd && mkdir -p build/sd/wiiu/apps/X");
    CHECK(wiiuupdate_extract("build/two.zip", "build/sd/wiiu/apps/X", "Mine.wuhb", NULL, NULL, err, sizeof err) == 0);
    CHECK(file_is("build/sd/wiiu/apps/X/MyApp.wuhb", "a") && file_is("build/sd/wiiu/apps/X/MyAppDebug.wuhb", "b"));
    CHECK(stat("build/sd/wiiu/apps/X/Mine.wuhb", &st) != 0);

    /* Replacing works when the target cannot simply be removed. */
    system("mkdir -p build/sd/r && echo new > build/sd/r/new && echo old > build/sd/r/old");
    CHECK(wiiuupdate_replace("build/sd/r/new", "build/sd/r/old") == 0 && file_is("build/sd/r/old", "new\n"));

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        static char buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        buf[n] = 0;
        fclose(f);
        printf("%s -> %s\n", argv[1], wiiuupdate_newest_tag(buf, tag, sizeof tag) ? "(none)" : tag);
        if (!wiiuupdate_newest_tag(buf, tag, sizeof tag) && !wiiuupdate_release_asset(buf, tag, asset, sizeof asset))
            printf("asset: %s\n", asset);
    }
    printf(failures ? "%d failures\n" : "ok\n", failures);
    return failures != 0;
}
