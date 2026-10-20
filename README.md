# libwiiuupdate

Checks GitHub for a newer release of a Wii U homebrew app (Aroma, `.wuhb`) and
can install it in place, built on wut. The Wii U counterpart of
[libwiiupdate](https://github.com/quatric/libwiiupdate).

- Runs on its own thread: connects to the network, asks the GitHub releases
  API over HTTPS and compares the newest release tag with the running version
- Ships its own small mbedTLS (TLS 1.2, certificate chain and host name
  verified against the bundled roots; dates are not checked since many
  console clocks are wrong). Its symbols are renamed inside the archive, so
  apps that link their own mbedTLS are unaffected
- Picks the highest numbered, non-prerelease tag, so rolling tags such as
  `latest` are ignored
- Failures are quiet: no network or no reply just means no notice
- Updates in place: downloads the release's Wii U zip (streamed to the SD
  card, so size does not matter), then unpacks its `wiiu/apps/<Name>/` folder
  over the running app's folder. Every file is written under a temporary name
  first, so a failed download or unpack leaves the installed copy untouched

## Build

Requires devkitPro with devkitPPC and wut.

```sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
make            # produces lib/libwiiuupdate.a
make install    # copies into $DEVKITPRO/portlibs/wiiu
```

## Usage

Start the check at launch and show a popup when it finds something:

```c
#include <wiiuupdate.h>

WIIUUPDATE_Start("quatric", "MyApp", APP_VERSION);

while (running) {
    wiiuupdate_popup pop;
    int st = WIIUUPDATE_Popup(vpad_buttons_down, &pop);
    if (st == WIIUUPDATE_POPUP_EXIT) running = false;  /* installed: back to the Wii U Menu */
    else if (st == WIIUUPDATE_POPUP_OPEN) {
        vpad_buttons_down = 0;                         /* the popup has the controls */
        draw_box(pop.title, pop.text, pop.detail, pop.hint);
        if (pop.progress >= 0) draw_progress(pop.progress);
    }
}

/* on the way out */
WIIUUPDATE_Cancel();
WIIUUPDATE_Wait(5000);
```

`WIIUUPDATE_Popup` takes `VPAD_BUTTON_*` bits; apps that read other
controllers map their A and B buttons onto `VPAD_BUTTON_A` and
`VPAD_BUTTON_B`. A updates (or closes the popup when there is nothing to
install), B closes it or cancels the download.

Apps that draw their own popup can drive it with `WIIUUPDATE_TakeNotice()`
(true once, when a newer release was found), `WIIUUPDATE_Message()`
(`"Version v1.3.0 is available (you have v1.2.0)."`), `WIIUUPDATE_Install()`,
`WIIUUPDATE_InstallState()`, `WIIUUPDATE_Progress()` and
`WIIUUPDATE_InstallError()`.

Apps that bring up the network themselves should call `WIIUUPDATE_Wait()`
first, so the two do not race.

### Installing

`WIIUUPDATE_Install()` picks the release's asset by file name:

1. a zip named for the Wii U (`wiiu`, `wup` or `aroma` in the name)
2. a `.wuhb`
3. any other zip, unless its name is for another console (`Wii` without a
   `U`, `3DS`, `Switch`)
4. an `.rpx`

so one release can carry the Wii, Wii U and 3DS builds side by side.

The target is the running app's own file. Under Aroma the library asks the
RPX loader where that is (`RL_GetPathOfRunningExecutable`, looked up at run
time, so nothing extra is linked); `WIIUUPDATE_SetAppPath()` overrides it.
The zip's `wiiu/apps/<Name>/` folder is unpacked into that file's folder, and
the zip's `.wuhb` is written under the running file's name, so a copy that
was renamed or moved out of its folder is still updated in place rather than
duplicated. Without a known path, the update goes to
`sd:/wiiu/apps/<Name>/`. Files in the app folder that the zip does not
contain, such as saved settings, are left alone.

Aroma keeps the running `.wuhb` open as `/vol/content`. If it cannot be
replaced as it is, the library unmounts the bundle through the RPX loader
(`RL_UnmountCurrentRunningBundle`) and tries again, then falls back to
writing the new contents over the old file. After that the app should not
read `/vol/content` any more; exit as soon as the popup says it is done.

### Other calls

* `WIIUUPDATE_State()`: `CHECKING`, `UP_TO_DATE`, `AVAILABLE`, `FAILED` or `CANCELLED`.
* `WIIUUPDATE_Wait(ms)`: block until the check (or install) is done.
* `WIIUUPDATE_Cancel()`: drop the result, or stop a download.
* `WIIUUPDATE_Latest()`, `WIIUUPDATE_Current()`, `WIIUUPDATE_Url()`, `WIIUUPDATE_CompareVersions(a, b)`.
* `WIIUUPDATE_InstallDir()`: the folder the update went to.

### Using it from an app

Add it as a git submodule, build it before the app and link it:

```sh
git submodule add https://github.com/quatric/libwiiuupdate.git libwiiuupdate
```

```make
LIBS    := -lwiiuupdate $(LIBS)        # ahead of -lwut
LIBDIRS += $(CURDIR)/libwiiuupdate     # include/ and lib/

# after the $(BUILD) rule, so it stays the default goal
$(BUILD): libwiiuupdate-lib
.PHONY: libwiiuupdate-lib
libwiiuupdate-lib:
	@$(MAKE) --no-print-directory -C libwiiuupdate
```

The library builds from its own sources (mbedTLS and miniz are vendored), so
CI needs nothing beyond devkitPPC with wut and `submodules: true` on checkout.

See `example/main.c` for a complete program.

## Tests

`tests/host_test.c` checks version comparison, release and asset parsing and
zip unpacking on a desktop; `build/host_check` runs the whole network path
against plain sockets for an end-to-end run against GitHub, and with an app
path (relative to `build/sd`, the stand-in SD card) also downloads and
installs the update there:

```sh
make -f tests/Makefile.host
./build/host_test
./build/host_check quatric PictoChat-Client 1.0.0
./build/host_check quatric PictoChat-Client 1.0.0 wiiu/apps/PictoChat/PictoChat.wuhb
```

## Root certificates

`source/wiiuupdate_certs.h` is generated from the Mozilla CA bundle:

```sh
curl -sSLf -o cacert.pem https://curl.se/ca/cacert.pem
python3 scripts/gen_certs.py cacert.pem > source/wiiuupdate_certs.h
```

## License

AGPL-3.0, see `LICENSE`. mbedTLS in `vendor/mbedtls` is Apache-2.0 (or
GPL-2.0-or-later), see `vendor/mbedtls/LICENSE`. miniz in `vendor/miniz` is
MIT, see `vendor/miniz/LICENSE`.
