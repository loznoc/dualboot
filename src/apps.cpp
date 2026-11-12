#include "apps.h"
#include "display.h" // IC_* icon ids
#include <Preferences.h>
#include <esp_rom_crc.h>

static void appSetChecksum(const char *label, uint32_t crc, uint32_t len);
#include <SD.h>
#include <algorithm>
#include <esp_app_format.h> // ESP_IMAGE_HEADER_MAGIC
#include <esp_ota_ops.h>

// Display names live in their own NVS namespace, keyed by the slot's partition label.
static const char *kNameNs = "dblapps";

String appStoredName(const char *label) {
    Preferences p;
    p.begin(kNameNs, true);
    String n = p.getString(label, "");
    p.end();
    return n;
}
static void appSetChecksum(const char *label, uint32_t crc, uint32_t len) {
    Preferences p;
    p.begin(kNameNs, false);
    p.putUInt((String("ck_") + label).c_str(), crc);
    p.putUInt((String("ln_") + label).c_str(), len);
    p.end();
}

void appSetName(const char *label, const String &name) {
    Preferences p;
    p.begin(kNameNs, false);
    p.putString(label, name);
    p.end();
}
void appClearName(const char *label) {
    Preferences p;
    p.begin(kNameNs, false);
    p.remove(label);
    p.remove((String("i") + label).c_str());   // its icon choice goes with it
    p.remove((String("ck_") + label).c_str()); // and the checksum, so a reinstall starts clean
    p.remove((String("ln_") + label).c_str());
    p.end();
}

// ---- per-slot icon --------------------------------------------------------------------------
// The icons a slot can be given. IC_APP (the rocket) stays first so it reads as the default.
static const int kAppIcons[] = {
    IC_APP,   IC_GHOST,  IC_ANTENNA, IC_BUG,      IC_SKULL, IC_GAMEPAD,
    IC_CHIP,  IC_KEY,    IC_SHIELD,  IC_TERMINAL, IC_NFC,   IC_WRENCH,
    IC_STAR,  IC_FLASH,  IC_USB,     IC_WIFI,     IC_PIN,   IC_THEME,
};

const int *appIconChoices(int &count) {
    count = (int)(sizeof(kAppIcons) / sizeof(kAppIcons[0]));
    return kAppIcons;
}

int appSlotIcon(const char *label) {
    Preferences p;
    p.begin(kNameNs, true);
    const int v = p.getInt((String("i") + label).c_str(), IC_APP);
    p.end();
    // Guard against a value saved by a build with a different icon table.
    return (v > IC_NONE && v < IC_COUNT_) ? v : IC_APP;
}

void appSetIcon(const char *label, int icon) {
    Preferences p;
    p.begin(kNameNs, false);
    p.putInt((String("i") + label).c_str(), icon);
    p.end();
}

String appSlotDisplay(const AppSlot &s) {
    if (!s.installed) return "Empty";
    String nm = appStoredName(s.part->label);
    if (nm.length()) return nm;
    // Installed but never named — e.g. written by an older launcher or flashed directly. It must
    // NOT read "Empty": the slot boots fine, so calling it empty makes a working app look missing.
    return String("App ") + (s.index + 1);
}

// Wipe the front of a slot so nothing is left that could pass for an installed app.
//
// esp_ota_begin() erases the slot and the image header goes down first, so an install that
// dies part-way leaves a partition whose first byte is still 0xE9. Clearing one erase block
// is enough: the header, the first segment header and the app descriptor all live inside it,
// and without them nothing downstream mistakes the remains for firmware.
static void invalidateSlot(const esp_partition_t *p) {
    if (!p) return;
    const uint32_t block = p->erase_size ? p->erase_size : 4096;
    if (block <= p->size) esp_partition_erase_range(p, 0, block);
}

// Is there a real application in this slot?
//
// The old test read one byte and compared it to 0xE9, which a half-written image, an image
// built for a different chip, or any file that happens to start with that byte all passed.
// Three cheap reads instead:
//
//   * the image header has to be ours — right magic, right chip, a sane segment count;
//   * the app descriptor has to be there, which means the first 288 bytes are a genuine
//     image and not a fragment. Bruce, the Flipper port and this launcher all carry one,
//     as does anything else built with ESP-IDF or Arduino.
//
// This stays cheap enough for the menu to call on every pass. Whether the image is complete
// is a different and far more expensive question - appSlotHealthy() answers that, and the
// launcher asks it before handing control over.
static bool slotHasImage(const esp_partition_t *p) {
    if (!p) return false;

    esp_image_header_t hdr;
    if (esp_partition_read(p, 0, &hdr, sizeof(hdr)) != ESP_OK) return false;
    if (hdr.magic != ESP_IMAGE_HEADER_MAGIC) return false;
    if (hdr.chip_id != ESP_CHIP_ID_ESP32S3) return false;
    if (hdr.segment_count == 0 || hdr.segment_count > 16) return false;

    esp_app_desc_t desc;
    return esp_ota_get_partition_description(p, &desc) == ESP_OK;
}

std::vector<AppSlot> appAllSlots() {
    std::vector<AppSlot> slots;
    esp_partition_iterator_t it =
        esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    while (it) {
        const esp_partition_t *p = esp_partition_get(it);
        if (p->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
            p->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_15) {
            AppSlot s;
            s.part = p;
            s.index = p->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0;
            s.installed = slotHasImage(p);
            String stored = appStoredName(p->label);
            s.name = stored.isEmpty() ? String(p->label) : stored;
            slots.push_back(s);
        }
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    // Order by OTA index so the menu is stable.
    std::sort(slots.begin(), slots.end(), [](const AppSlot &a, const AppSlot &b) {
        return a.index < b.index;
    });
    return slots;
}

std::vector<AppSlot> appsList() {
    std::vector<AppSlot> out;
    for (const AppSlot &s : appAllSlots())
        if (s.installed) out.push_back(s);
    return out;
}

bool appBoot(const esp_partition_t *part) {
    if (!part) return false;
    if (esp_ota_set_boot_partition(part) != ESP_OK) return false;
    delay(120);
    esp_restart(); // software reset -> custom bootloader boots the selected OTA slot
    return true;   // not reached
}

bool appDelete(const esp_partition_t *part) {
    if (!part) return false;
    // Erase the header before the rest: a 4.5 MB erase takes seconds, and if the device
    // loses power in the middle of it the slot must already have stopped looking bootable.
    invalidateSlot(part);
    appClearName(part->label);
    return esp_partition_erase_range(part, 0, part->size) == ESP_OK;
}

// ---- firmware image sniffing ------------------------------------------------------------------
// A .bin can be either a FULL flash image (bootloader at 0, partition table at 0x8000, app at
// 0x10000) or just the APP image on its own — plenty of projects publish the raw build output.
// Assuming one or the other is why installs failed, so the layout is detected instead.

static const uint16_t kChipEsp32S3 = 0x0009;

// Exact length of the ESP32 image starting at `off`, or 0 if there isn't one there. Walking the
// segment table also means we write only the image, never trailing data from a merged file.
static size_t espImageLen(File &f, size_t off, size_t total, uint16_t &chip) {
    uint8_t h[24];
    if (off + sizeof(h) > total || !f.seek(off) || f.read(h, sizeof(h)) != (int)sizeof(h)) return 0;
    if (h[0] != 0xE9) return 0;
    const uint8_t segs = h[1];
    if (segs == 0 || segs > 16) return 0;
    chip = (uint16_t)h[12] | ((uint16_t)h[13] << 8);
    const bool hashed = h[23] == 1;

    size_t pos = sizeof(h);
    for (uint8_t i = 0; i < segs; ++i) {
        uint8_t s[8];
        if (!f.seek(off + pos) || f.read(s, sizeof(s)) != (int)sizeof(s)) return 0;
        const uint32_t len = (uint32_t)s[4] | ((uint32_t)s[5] << 8) | ((uint32_t)s[6] << 16) |
                             ((uint32_t)s[7] << 24);
        if (len > 16u * 1024 * 1024) return 0; // nonsense length: not really an image
        pos += 8 + len;
        if (off + pos > total) return 0;
    }
    pos = ((pos + 16) & ~(size_t)15); // 1-byte checksum, padded to a 16-byte boundary
    if (hashed) pos += 32;            // appended SHA-256
    return (off + pos <= total) ? pos : 0;
}

bool appInstallFromSd(const char *path, const esp_partition_t *slot, const String &name,
                      InstallProgress cb, String *err) {
    auto fail = [&](const String &m) {
        if (err) *err = m;
        return false;
    };
    if (!slot) return fail("No slot");
    File f = SD.open(path);
    if (!f) return fail("Cannot open file");
    const size_t total = f.size();

    // 0x10000 first: a full flash image ALSO has a valid image at 0 (its bootloader), and
    // installing that instead of the app would produce a slot that never boots.
    // 0x10000 first, because a full flash image also has a valid bootloader image sitting at
    // 0 and we want the app, not the bootloader. A candidate built for this chip always wins
    // over one that merely parses, so a file that happens to look like an image at the wrong
    // offset cannot shadow the real app behind it.
    const size_t candidates[] = {0x10000, 0, 0x20000};
    size_t appOff = 0, appSize = 0;
    uint16_t chip = 0;
    for (size_t c : candidates) {
        uint16_t ch = 0;
        const size_t len = espImageLen(f, c, total, ch);
        if (!len) continue;
        if (!appSize || (chip != kChipEsp32S3 && ch == kChipEsp32S3)) {
            appOff = c;
            appSize = len;
            chip = ch;
        }
        if (chip == kChipEsp32S3) break;
    }
    if (!appSize) {
        f.close();
        return fail("Not an ESP32 firmware");
    }
    if (chip != kChipEsp32S3) {
        f.close();
        char b[48];
        snprintf(b, sizeof(b), "Wrong chip (id 0x%04X)", chip);
        return fail(b);
    }
    if (appSize > slot->size) {
        f.close();
        char b[56];
        snprintf(b, sizeof(b), "Too big: %u KB, slot %u KB", (unsigned)(appSize / 1024),
                 (unsigned)(slot->size / 1024));
        return fail(b);
    }
    if (!f.seek(appOff)) {
        f.close();
        return fail("Cannot read file");
    }

    // From here on the slot's old contents are gone, so every exit below has to leave it
    // either holding the new image or holding nothing at all.
    esp_ota_handle_t h;
    if (esp_ota_begin(slot, appSize, &h) != ESP_OK) { // erases the slot, then streams in
        f.close();
        appClearName(slot->label);
        invalidateSlot(slot);
        return fail("Cannot erase slot");
    }

    static uint8_t buf[4096];
    size_t done = 0;
    bool ok = true;
    uint32_t crc = 0;
    while (done < appSize) {
        const size_t want = min(sizeof(buf), appSize - done);
        const int n = f.read(buf, want);
        if (n <= 0) {
            ok = false;
            break;
        }
        if (esp_ota_write(h, buf, n) != ESP_OK) {
            ok = false;
            break;
        }
        crc = esp_rom_crc32_le(crc, buf, n);
        done += n;
        if (cb) cb(done, appSize);
        yield();
    }
    f.close();

    // esp_ota_begin() already erased the slot, so a failure here leaves it genuinely blank —
    // whatever used to be installed is gone. Drop its name/icon too, otherwise the slot keeps
    // advertising an app that no longer exists.
    if (!ok) {
        esp_ota_abort(h);
        appClearName(slot->label);
        invalidateSlot(slot); // otherwise the header we already wrote still reads as an app
        return fail("Read/write error");
    }
    if (esp_ota_end(h) != ESP_OK) { // image failed validation; the slot is unusable
        appClearName(slot->label);
        invalidateSlot(slot);
        return fail("Image rejected (corrupt?)");
    }
    appSetName(slot->label, name);
    appSetChecksum(slot->label, crc, appSize);
    return true;
}

// ---- slot integrity --------------------------------------------------------------------

bool appSlotHasChecksum(const esp_partition_t *part) {
    if (!part) return false;
    Preferences p;
    p.begin(kNameNs, true);
    const bool has = p.isKey((String("ln_") + part->label).c_str());
    p.end();
    return has;
}

bool appSlotHealthy(const esp_partition_t *part, String *err) {
    if (!part) return false;
    Preferences p;
    p.begin(kNameNs, true);
    const String kc = String("ck_") + part->label, kl = String("ln_") + part->label;
    const bool has = p.isKey(kl.c_str());
    const uint32_t want = p.getUInt(kc.c_str(), 0);
    const uint32_t len = p.getUInt(kl.c_str(), 0);
    p.end();

    // Nothing recorded (installed by an older launcher, or flashed directly) — do not stand
    // in the user's way over something we never measured.
    if (!has || len == 0) return true;
    if (len > part->size) {
        if (err) *err = "Recorded size does not fit the slot";
        return false;
    }

    static uint8_t buf[4096];
    uint32_t crc = 0, done = 0;
    while (done < len) {
        const uint32_t want_n = min((uint32_t)sizeof(buf), len - done);
        if (esp_partition_read(part, done, buf, want_n) != ESP_OK) {
            if (err) *err = "Could not read the slot";
            return false;
        }
        crc = esp_rom_crc32_le(crc, buf, want_n);
        done += want_n;
        yield();
    }
    if (crc != want) {
        if (err) *err = "Checksum does not match - the image is damaged";
        return false;
    }
    return true;
}
