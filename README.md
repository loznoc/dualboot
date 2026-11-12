# Dual-Boot Launcher — LilyGo T-Embed CC1101

A clean, PIN-lockable **launcher firmware** for the LilyGo **T-Embed CC1101** (ESP32-S3).
The device always boots into the launcher first; from there you install and run any ESP32-S3
firmware (Bruce, Flipper-style tools, Marauder, your own builds…) straight from the SD card.
Nothing but the launcher ships on the device — you add the rest.

![UI preview](docs/img/preview.png)

> The screens above are rendered previews of the on-device UI (320×170 display).

---

## Features

- **Launcher-first boot.** A custom 2nd-stage bootloader always returns to the launcher on a
  normal reset, so you can never get "locked into" an installed app — power-cycle and you're back.
- **Install firmware from the SD card.** Drop any `.bin` in the card root, pick a slot, name it,
  done. Three app slots.
- **USB drive mode.** Expose the SD card as a USB mass-storage drive to copy files from your PC
  without removing it.
- **Wi-Fi portal with screen mirror + remote.** The launcher serves a small page over Wi-Fi:
  watch its screen live from a phone, drive the wheel and the side button from the browser, and
  drop firmware `.bin` files and theme packs onto the card — no cable, no card reader. It can
  host its own hotspot or join your normal network, and answers to `tembed.local`.
- **Web-flashable.** Install the launcher onto a fresh device from the browser — no toolchain.
- **Themeable.** Full-spectrum accent-colour picker, WS2812 LED colour + brightness (0 % = off),
  coordinated presets, and **complete custom theme packs** — colours, your own icons and your own
  boot animation — built in the browser with the [theme creator](https://loznoc.github.io/dualboot/theme.html).
- **Custom boot animation.** Ships with a built-in animation; drop a GIF into the theme creator (or
  your own frames on the SD card) to replace it. Can be turned off.
- **Optional wheel-pattern PIN lock**, battery percentage + charging indicator, power-off.
- **Bauhaus / Flipper / hacker** look: one big sliding icon per screen, a bitmap "Cyberjunkies"
  title font, crisp Material icons, and an Oreo-cat mascot on the install screen.

---

## Install (recommended: web flasher)

The easiest way to put the launcher on a **fresh** T-Embed CC1101, straight from a browser:

1. Enable **GitHub Pages** for this repo: *Settings → Pages → Build from branch → `main` / `docs`*.
   You'll get a URL like `https://<you>.github.io/<repo>/`.
2. Open that URL in **Chrome or Edge** on desktop, plug in the device, click **Install**, pick the
   serial port. It erases and flashes, and the device boots into the launcher.

Prefer to do it yourself? Grab [`firmware/launcher.bin`](firmware/launcher.bin) and flash it at
offset `0x0`:

```bash
esptool --chip esp32s3 write_flash 0x0 launcher.bin
```

`launcher.bin` is a complete image (custom bootloader + partition table + launcher app) — it does
**not** contain Bruce/Flipper/anything else.

> **Updating a device that already runs this launcher:** its USB runs in OTG mode, which blocks a
> flasher's auto-reset. On the device go to **Settings → Power → Flash (web) → press** to enter
> download mode first, then flash. A fresh device needs no such step; a power-cycle cancels it.

The web flasher writes the three pieces at their own offsets (`0x0`, `0x8000`, `0x10000`) rather
than one image across the front of the flash. That deliberately steps around the settings area, so
an update keeps your PIN, theme, slot names and slot icons. The single-file `launcher.bin` does
*not* — it writes `0xFF` across `0x9000–0x10000` and those all go back to defaults. Either way the
app slots start at `0x1A0000` and are never touched, so installed firmware survives.

### Updating an existing device — the Bruce settings fix

Launchers flashed before v1.1 shipped a partition table with **no LittleFS partition**. Bruce
saves its config there, so every setting you changed was silently dropped on the next boot —
nothing on screen said anything had gone wrong. It looked like a Bruce bug; it was this table.

The fix is one line in `partitions.csv`, and applying it is *only* the table — 3 KB, nothing else
touched:

```bash
esptool --chip esp32s3 --port COM5 write-flash 0x8000 partitions.bin
```

[`docs/partitions.bin`](docs/partitions.bin) is the file; enter download mode as above first.
Afterwards the first boot into Bruce formats the fresh filesystem itself and writes a default
config, so you set your Bruce settings one more time — and from then on they stick.

---

## Using it

**Main menu** (turn the wheel to move, press to select, top button = back):

- **Slot 1–3** — an installed app (press to boot it) or `Empty` (press to install into it). Each
  slot carries its own icon, chosen when you install it or later under **Settings → Icons**.
- **Install** — pick a `.bin` from the SD card, choose a slot, name it.
- **USB** — expose the SD card as a USB drive.
- **WiFi** — start the upload hotspot (see below).
- **Settings** — design, slot icons, delete apps, PIN, about, power.

**Booting an app:** selecting an installed slot sets it as the boot target and restarts. The
custom bootloader honours that choice on the *software* restart, so the app runs. On the next
power-cycle you're back in the launcher automatically.

### Installing firmware from SD

Copy any ESP32-S3 app `.bin` to the **root** of the SD card (or use USB drive mode). Then
**Install → pick the file → pick a slot → name it**. The name defaults to the file name (press to
accept) and there's a `DEL` entry on the wheel for editing; the top button cancels a step.

Only the app image is written into the slot, so a standard single-file `.bin` (bootloader + table
+ app) is handled correctly.

### USB drive mode

**USB** on the main menu presents the SD card to your PC as a removable drive. Copy firmware,
themes, or boot frames, then press back — the launcher re-reads the card.

### Wi-Fi portal — mirror, remote, uploads

**WiFi** on the main menu brings up a small page that does three things: shows the screen live,
drives the device, and takes file uploads.

1. Pick **WiFi**. First time it opens its own hotspot: the screen shows the network name
   (`T-Embed-XXXX`), the password (`dualboot`) and the address (`http://192.168.4.1`).
2. Join it and open that address.
3. In the **NETWORK** panel put in your normal Wi-Fi. From then on the launcher *joins* that
   network instead of opening a hotspot, so your phone never has to switch networks — and it
   answers to `http://tembed.local` as well as its address. Tick **start wi-fi at boot** and it is
   reachable straight after a reset (only after the PIN, so the lock screen is never exposed).
4. **SCREEN** mirrors the framebuffer as raw RGB565, up to the full 320×170, with a quality
   switch when you want it lighter. **REMOTE** injects the same four events the wheel and the
   side button produce.
5. Upload a **firmware `.bin`** (saved to the card — install it with **Install**) or a **theme pack
   `.zip`** straight from the theme creator (unpacked into `/catcolor` on the device).
6. **BACK** hands the screen back and leaves Wi-Fi running, so the mirror keeps working while you
   navigate the device normally. **Press** turns Wi-Fi off. Neither quits mid-upload.

The endpoints are plain HTTP if you would rather script them: `GET /fb?s=1..4` (raw RGB565, with
`X-Width`/`X-Height` headers), `GET /key?k=left|right|ok|back`, `GET|POST /cm?cmnd=nav
prev|next|sel|esc` (the same names Bruce's WebUI uses), `GET|POST /net`, `POST /bin`,
`POST /theme`.

Theme zips are unpacked on-device. They must be *stored* (uncompressed) zips — which is exactly
what the theme creator produces. Entries with `..` or absolute paths are rejected.

### Themes & colours (Settings → Design)

- **Theme** — coordinated presets *plus any custom themes from the SD card* (see below). Each entry
  shows its own colour.
- **Color** — the UI accent colour; scroll through the full spectrum.
- **LED Color** — the WS2812 strip colour (or off), full spectrum.
- **Bright** — LED brightness; turn for ±5 %, press to save. **0 % turns the LEDs off.**
- **Boot** — turn the boot animation on/off, preview it, or read the frame requirements.

#### Make a theme — the easy way

Open the **[theme creator](https://loznoc.github.io/dualboot/theme.html)**, pick your colours,
optionally replace any icons with your own images, optionally drop in a **GIF** for the boot
animation, and download the pack. Everything runs in your browser — nothing is uploaded.

Then unzip it into a **`/catcolor`** folder on the SD card (use **USB** drive mode so you don't
have to take the card out) and pick it under **Settings → Design → Theme**. Colours, icons and the
boot animation all switch over at once.

> The folder is `/catcolor`, **not** `/themes` — Bruce already uses `/themes` for its own themes
> and the two formats are unrelated, so they are kept apart.

#### Theme format (if you'd rather write it yourself)

A theme is either a single `.txt` file in `/catcolor`, or a **folder** — a "pack" — that can also
carry icons and an animation:

```
/catcolor/cyberpunk/theme.txt      colours (required)
/catcolor/cyberpunk/icons.bin      custom icon set   (optional)
/catcolor/cyberpunk/boot/*.raw     boot animation    (optional)
```

`theme.txt` is plain `key = value`, `#` starts a comment:

```ini
name = Cyberpunk
accent = 00E5FF      # screen / UI colour, hex RRGGBB
led    = FF00AA      # LED strip colour (optional, defaults to accent)
brightness = 70      # optional, 0-100 (0 = LEDs off)
```

`icons.bin` is `"TEIC"`, version `1`, icon count, width, height (all single bytes after the magic),
followed by that many **48 × 48 one-bit masks** (6 bytes per row, MSB first) in the order of
`enum Icon` in `src/display.h`. The launcher tints them to the theme, so only the shape matters.

**Boot frames** live in the pack's `boot/` folder, or in a top-level **`/boot`** folder to override
the animation regardless of theme. They play in file-name order; each frame is one `.raw` file:

- 2 bytes width + 2 bytes height (little-endian),
- then `width × height` pixels, **RGB565**, little-endian.
- Max size **320 × 170**; frames are scaled to the screen. ~20 fps.

### PIN lock

The launcher can be locked with a **pattern of wheel turns** (left/right) instead of digits — it's
**off by default**. Turn it on, change it, or turn it back off under **Settings → PIN**; with it off
the launcher boots straight to the menu with no prompt.

---

## How it works

The trick behind "always boot the launcher, but still boot into apps" is a small **custom
2nd-stage bootloader** (`bootloader/`):

- On **every reset** it boots the launcher (the `test`-type partition) …
- **except a software restart** (`esp_restart()`), where it honours the OTA boot-partition choice.

So the launcher boots an app by calling `esp_ota_set_boot_partition(slot)` and then
`esp_restart()`. That software restart runs the app. Any later power-cycle / button reset is *not*
a software restart, so the bootloader falls back to the launcher. You can't get stuck in an app.

### Partition layout (`partitions.csv`)

| Name     | Type | SubType  | Offset    | Size     | Purpose                      |
|----------|------|----------|-----------|----------|------------------------------|
| nvs      | data | nvs      | 0x9000    | 0x5000   | settings, names, PIN         |
| otadata  | data | ota      | 0xE000    | 0x2000   | OTA boot selection           |
| launcher | app  | test     | 0x10000   | 0x180000 | **the launcher itself**      |
| coredump | data | coredump | 0x190000  | 0x10000  | crash dumps                  |
| app0/1/2 | app  | ota_0/1/2| 0x1A0000… | 0x480000 | the three install slots      |
| spiffs   | data | spiffs   | 0xF20000  | 0xE0000  | LittleFS for installed apps  |

The `spiffs` partition is not for the launcher — it never touches it. It is there because almost
every ESP32 firmware expects to find a LittleFS to keep its settings in, and without one those
saves fail silently (see the Bruce fix above). It lives in the 896 KB at the end of the flash
that nothing else used, so no app slot had to move and nothing needs reinstalling.

---

## Checking what you downloaded

Every push builds the firmware on a clean GitHub runner, and each run's summary lists the
SHA-256 of all four images along with the commit they came from. To check a file you
downloaded, hash it and compare:

```bash
sha256sum launcher.bin
```

Each run also carries a signed provenance attestation naming the commit and the workflow
that produced it:

```bash
gh attestation verify launcher.bin --repo loznoc/dualboot
```

**What this does not claim.** The images are not bit-for-bit reproducible and cannot be:
every ESP-IDF application embeds an `esp_app_desc_t` holding the build date, the build time
and a hash of the ELF, and the image's trailing SHA-256 covers all of it. Two builds of the
same commit differ by a few dozen bytes. What is reproducible is the partition table —
`partitions.bin` comes out byte-identical wherever it is built, which is the part that
decides where everything lives.

## Building from source

Requires [PlatformIO](https://platformio.org/). The project pins the
[pioarduino](https://github.com/pioarduino/platform-espressif32) platform and a **custom
`framework-arduinoespressif32-libs`** build (by [bmorcelli](https://github.com/bmorcelli)) that is
compiled with `CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED=y` — the launcher needs this to write the
OTA slots; the stock package aborts. Both are referenced in `platformio.ini`, so a plain build
pulls them automatically.

```bash
pio run -e lilygo-t-embed-cc1101
```

That produces the launcher **app** at `.pio/build/lilygo-t-embed-cc1101/firmware.bin`. The
distributable `firmware/launcher.bin` is that app merged with the custom bootloader and partition
table:

```bash
esptool --chip esp32s3 merge_bin -o firmware/launcher.bin \
  0x0      bootloader/bootloader.bin \
  0x8000   .pio/build/lilygo-t-embed-cc1101/partitions.bin \
  0x10000  .pio/build/lilygo-t-embed-cc1101/firmware.bin
```

A prebuilt `firmware/launcher.bin` is committed so you don't have to.

### Layout

```
src/            launcher source (display, input, apps, settings, USB, Wi-Fi portal, PIN,
                boot animation …) + generated data headers (font, icons, cat, boot animation)
lib/            vendored Arduino_GFX (PSRAM-canvas patched) and RotaryEncoder
boards/         the lilygo-t-embed-cc1101 board variant
bootloader/     custom 2nd-stage bootloader (prebuilt .bin + source)
partitions.csv  the layout above
firmware/       prebuilt launcher.bin
tools/          build-time helpers: the Wi-Fi portal page and the script that bakes it into
                src/webpage_data.h, the image assembler, the flasher, and the generators
                for the font/icon/animation data headers
docs/           GitHub Pages: the web flasher (index.html + the three .bin parts), the
                theme creator (theme.html) and the built-in icon set it starts from
```

---

## Credits & licenses

This launcher is a clean, from-scratch firmware, but it stands on great work by others:

- **[bmorcelli](https://github.com/bmorcelli)** — the M5Stack Launcher that inspired the dual-boot
  concept, and the custom `esp32-arduino-libs` build that makes on-device flashing possible.
- **Arduino_GFX** by [moononournation](https://github.com/moononournation/Arduino_GFX) — display
  driver (vendored in `lib/`, with a small PSRAM canvas patch). See its `license.txt`.
- **RotaryEncoder** by [mathertel](https://github.com/mathertel/RotaryEncoder) — wheel decoding.
- **Material Design Icons** by [Pictogrammers](https://pictogrammers.com/library/mdi/) — the UI
  icons (Apache-2.0), rasterized into `src/icons_data.h`.
- **Cyberjunkies** font — the title typeface, rasterized into `src/cyberfont_data.h`.
- **Oreo Cat** by **Aichan_owo** — the install-screen mascot (`src/cat_data.h`) and the cat on
  the Wi-Fi portal page (`tools/assets/cat-stealth.gif`).
- **Silkscreen** by [Jason Kottke](https://github.com/googlefonts/silkscreen) — the pixel typeface
  on the Wi-Fi portal page, subset to ASCII and embedded in the page so it renders with no
  internet. SIL Open Font License 1.1, see `tools/assets/OFL.txt`.

The launcher's own code is released under the MIT License (see `LICENSE`). Bundled components keep
their respective licenses. Firmware you install (Bruce, Flipper tools, Marauder, …) is not part of
this project.
