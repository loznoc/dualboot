// ======================================================================
//  T-Embed CC1101 dual-boot launcher
//
//  Cold boot -> boot animation -> PIN -> menu. The launcher ships on its own;
//  firmware is installed from the SD card into one of three OTA slots. Picking an
//  app sets its slot and issues a software restart; the custom bootloader boots
//  that slot on a software reset and returns here on the next power-cycle.
// ======================================================================
#include "apps.h"
#include "board.h"
#include "bootanim.h"
#include "display.h"
#include "input.h"
#include "led.h"
#include "pin.h"
#include "sdcard.h"
#include "serial_console.h"
#include "settings.h"
#include "usbdrive.h"
#include "webportal.h"
#include <SD.h>
#include <ctype.h>

// The Arduino loop task defaults to an 8 KB stack, which the graphics + SD + install
// paths overrun (stack-canary panic on boot). 16 KB gives comfortable headroom.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// Idle work for every menu: the serial console plus the web portal, so screen mirroring and the
// web remote keep working while you navigate the UI normally.
static void uiIdle() {
    serialConsolePoll();
    webPortalPoll();
}

static String g_installName;
static int g_lastPct = -1;
static uint32_t g_catT0 = 0, g_lastDraw = 0;
static void drawInstallProgress(uint32_t done, uint32_t total) {
    const int pct = total ? (int)((uint64_t)done * 100 / total) : 0;
    const uint32_t now = millis();
    if (pct == g_lastPct && now - g_lastDraw < 110) return; // redraw on % change or to animate the cat
    g_lastPct = pct;
    g_lastDraw = now;
    const int frame = (int)((now - g_catT0) / (uint32_t)uiCatDelayMs(CAT_ID_ATTACK));
    uiInstallScreen(g_installName, pct, frame);
}

// "marauder.bin" -> "Marauder".
static String deriveName(const String &file) {
    String n = file;
    const int dot = n.lastIndexOf('.');
    if (dot > 0) n = n.substring(0, dot);
    n.trim();
    if (n.length()) n = String((char)toupper(n[0])) + n.substring(1);
    if (n.length() > 16) n = n.substring(0, 16);
    return n;
}

// Let the user choose the icon a slot shows in the main menu. Returns true if one was picked.
static bool pickSlotIcon(const AppSlot &s, const String &name) {
    int n = 0;
    const int *choices = appIconChoices(n);
    const int current = appSlotIcon(s.part->label);
    std::vector<Tile> tiles;
    for (int i = 0; i < n; ++i)
        tiles.push_back({choices[i] == current ? String("Current") : name, choices[i], 0, 0});
    tiles.push_back({"Back", IC_BACK, 0, 0});
    const int sel = uiCarousel(tiles, "ICON", true);
    if (sel < 0 || sel >= n) return false;
    appSetIcon(s.part->label, choices[sel]);
    return true;
}

// Change the icon of any installed slot (Settings -> Icons).
static void slotIconsMenu() {
    for (;;) {
        std::vector<AppSlot> apps = appsList();
        if (apps.empty()) {
            uiMessage("No apps installed", COL_MUTED);
            delay(1200);
            return;
        }
        std::vector<Tile> tiles;
        for (const AppSlot &a : apps)
            tiles.push_back({appSlotDisplay(a), appSlotIcon(a.part->label), 0, 0});
        tiles.push_back({"Back", IC_BACK, 0, 0});
        const int sel = uiCarousel(tiles, "SLOT ICONS", true);
        if (sel < 0 || sel >= (int)apps.size()) return;
        pickSlotIcon(apps[sel], appSlotDisplay(apps[sel]));
    }
}

// Check a slot before handing control to it. An install that died halfway leaves an image
// that still looks bootable from the outside; the recorded checksum is the only thing that
// knows better. Returns false when the user should stay in the menu.
static bool bootSlot(const AppSlot &slot) {
    if (appSlotHasChecksum(slot.part)) {
        uiMessage("Checking " + appSlotDisplay(slot) + " ...", COL_MUTED);
        String err;
        if (!appSlotHealthy(slot.part, &err)) {
            uiBackground();
            uiTitleBar("SLOT DAMAGED");
            uiTextCenter(appSlotDisplay(slot), 40, 2, COL_ERR);
            uiTextCenter(err, 68, 1, COL_MUTED);
            uiTextCenter("Install it again from the SD card.", 88, 1, COL_MUTED);
            uiTextCenter("press = back", scrH() - 12, 1, gAccent);
            uiFlush();
            inputDrain();
            while (inputPoll() == EV_NONE) delay(30);
            return false;
        }
    }
    uiMessage(String("Starting ") + slot.name, COL_OK);
    appBoot(slot.part); // sets the slot + reboots; does not return
    return true;
}

// Pick any .bin from the SD card and write it into a chosen slot (so the launcher is not
// tied to Bruce/Flipper — anything in the card root shows up here).
static void installFromSdMenu() {
    if (!sdInit()) {
        uiError("No SD card");
        delay(1600);
        return;
    }
    std::vector<String> bins = sdListBins();
    if (bins.empty()) {
        uiError("No .bin files on SD");
        delay(1800);
        return;
    }

    std::vector<String> fitems = bins;
    fitems.push_back("Back");
    int fsel = uiMenu("PICK A .BIN", fitems);
    if (fsel < 0 || fsel >= (int)bins.size()) return;
    const String file = bins[fsel];

    std::vector<AppSlot> slots = appAllSlots();
    for (;;) { // slot -> name, so BACK during naming steps back to the slot picker
        std::vector<String> sitems;
        for (const AppSlot &s : slots)
            sitems.push_back(String("Slot ") + (s.index + 1) + ": " +
                             (s.installed ? s.name : String("empty")));
        sitems.push_back("Back");
        int ssel = uiMenu("INSTALL TO", sitems);
        if (ssel < 0 || ssel >= (int)slots.size()) return; // back from slots -> exit

        // Name is pre-filled with the derived name and the wheel defaults to SAVE, so one press
        // accepts it. BACK cancels (empty) and returns us to the slot picker above.
        String name = uiTextEntry("NAME", deriveName(file));
        if (name.isEmpty()) continue; // cancelled -> step back to the slot picker

        g_installName = name;
        g_lastPct = -1;
        g_lastDraw = 0;
        g_catT0 = millis();
        uiInstallScreen(name, 0, 0);
        String err;
        const bool ok =
            appInstallFromSd(("/" + file).c_str(), slots[ssel].part, name, drawInstallProgress, &err);
        if (ok) {
            uiMessage(name + " installed", COL_OK);
            delay(1200);
            pickSlotIcon(slots[ssel], name); // offer an icon while we are here; BACK keeps the default
        } else {
            // Say WHY. "Install failed" alone leaves people with nothing to act on.
            uiBackground();
            uiTitleBar("INSTALL FAILED");
            uiTextCenter(file, 40, 1, COL_MUTED);
            uiTextCenter(err.length() ? err : String("Unknown error"), 60, 2, COL_ERR);
            uiTextCenter("The slot is now empty.", 88, 1, COL_MUTED);
            uiTextCenter("press = back", scrH() - 12, 1, gAccent);
            uiFlush();
            inputDrain();
            while (inputPoll() == EV_NONE) delay(30);
        }
        return;
    }
}

// Uninstall a firmware: erase its slot and forget its name.
static void deleteAppsMenu() {
    for (;;) {
        std::vector<AppSlot> apps = appsList();
        if (apps.empty()) {
            uiMessage("No apps installed", COL_MUTED);
            delay(1200);
            return;
        }
        std::vector<String> items;
        for (const AppSlot &a : apps) items.push_back(appSlotDisplay(a)); // same as home grid
        items.push_back("Back");
        int sel = uiMenu("Delete app", items);
        if (sel < 0 || sel >= (int)apps.size()) return;

        std::vector<String> confirm = {String("Delete ") + apps[sel].name, "Cancel"};
        if (uiMenu("Sure?", confirm) != 0) continue;

        uiMessage(String("Deleting ") + apps[sel].name + " ...", COL_WARN);
        appDelete(apps[sel].part);
        uiMessage("Deleted", COL_OK);
        delay(900);
    }
}

static void aboutScreen() {
    gfx->fillScreen(COL_BG);
    uiTitleBar("ABOUT");

    uiTextCenter("This launcher stands on the work of", 38, 1, COL_MUTED);
    uiTextCenter("bmorcelli", 52, 1, gAccent);
    uiTextCenter("his M5Stack Launcher (the original) and", 74, 1, COL_MUTED);
    uiTextCenter("the esp32-arduino-libs build it needs.", 86, 1, COL_MUTED);
    uiTextCenter("github.com/bmorcelli  -  thank you!", 104, 1, COL_FG);
    uiTextCenter("T-Embed CC1101 dual-boot port by @loznoc", 122, 1, COL_MUTED);

    uiTextCenter("press BACK", scrH() - 12, 1, gAccent);
    uiFlush();
    inputDrain();
    while (inputPoll() != EV_BACK) {
        serialConsolePoll();
        delay(30);
    }
}

// Reboot into USB download mode so a browser web-flasher can reprogram the device.
static void flashModeScreen() {
    gfx->fillScreen(COL_BG);
    uiTitleBar("FLASH");
    uiTextCenter("Reprogram over USB from a browser -", 34, 1, COL_MUTED);
    uiTextCenter("no PC tools needed.", 46, 1, COL_MUTED);
    gfx->setTextSize(1);
    gfx->setTextColor(COL_FG, COL_BG);
    gfx->setCursor(14, 66);
    gfx->print("1. Press to enter USB download mode");
    gfx->setCursor(14, 80);
    gfx->print("2. Open the web flasher in Chrome/Edge");
    gfx->setCursor(14, 92);
    gfx->print("3. Click Install and pick the port");
    gfx->setCursor(14, 110);
    gfx->print("Power-cycle to cancel download mode.");
    uiTextCenter("press = download mode    back = cancel", scrH() - 12, 1, gAccent);
    uiFlush();
    inputDrain();
    for (;;) {
        InputEvent e = inputPoll();
        if (e == EV_BACK) return;
        if (e == EV_PRESS) {
            uiMessage("USB download mode", COL_OK);
            usbEnterDownloadMode(); // reboots into the ROM download port; does not return
        }
        delay(30);
    }
}

// Power off + web-flash mode live together so Settings stays a clean 6-tile grid.
static void powerMenu() {
    std::vector<Tile> tiles = {
        {"Power off", IC_OFF, 0, 0}, {"Flash (web)", IC_FLASH, 0, 0}, {"Back", IC_BACK, 0, 0}};
    int sel = uiCarousel(tiles, "POWER", true);
    if (sel == 0) powerOffDevice();
    else if (sel == 1) flashModeScreen();
}

static void settingsGrid() {
    for (;;) {
        std::vector<Tile> tiles = {
            {"Design", IC_DESIGN, 0}, {"Icons", IC_STAR, 0},  {"Delete", IC_DELETE, 0},
            {"PIN", IC_PIN, 0},       {"About", IC_ABOUT, 0}, {"Power", IC_OFF, 0},
            {"Back", IC_BACK, 0},
        };
        int sel = uiGrid("SETTINGS", tiles, uiIdle);
        if (sel < 0 || sel == 6) return;
        switch (sel) {
            case 0: appearanceMenu(); break;
            case 1: slotIconsMenu(); break;
            case 2: deleteAppsMenu(); break;
            case 3: pinSettingsMenu(); break;
            case 4: aboutScreen(); break;
            case 5: powerMenu(); break;
        }
    }
}

static void mainMenu() {
    for (;;) {
        std::vector<AppSlot> slots = appAllSlots();
        std::vector<Tile> tiles;
        for (const AppSlot &s : slots) {
            Tile t;
            t.badge = s.installed ? s.index + 1 : 0;        // number only on filled slots
            t.label = appSlotDisplay(s);                    // shared with the delete menu
            t.icon = s.installed ? appSlotIcon(s.part->label) : IC_EMPTY;
            tiles.push_back(t);
        }
        const int nSlots = (int)slots.size();
        tiles.push_back({"Install", IC_INSTALL, 0});
        tiles.push_back({"USB", IC_USB, 0});
        tiles.push_back({"WiFi", IC_WIFI, 0});
        tiles.push_back({"Settings", IC_SETTINGS, 0});

        int sel = uiGrid(LAUNCHER_NAME, tiles, uiIdle);
        if (sel < 0) continue;

        if (sel < nSlots) {
            if (slots[sel].installed) {
                if (!bootSlot(slots[sel])) continue;
            } else {
                installFromSdMenu();
            }
        } else if (sel == nSlots) {
            installFromSdMenu();
        } else if (sel == nSlots + 1) {
            usbDriveEnter();
        } else if (sel == nSlots + 2) {
            webPortalEnter();
        } else {
            settingsGrid();
        }
    }
}

void setup() {
    displayInit();
    usbSetup(); // USB CDC console + SD-backed MSC (needs the display's SPI bus for the SD)
    inputInit();
    inputSetIdleHook(uiIdle); // every screen that waits for input now also serves the network
    settingsLoad(); // apply saved accent colour + LED colour

    if (bootAnimEnabled()) playBootAnimation();

    if (!pinFlow()) {
        uiError("Locked");
        for (;;) delay(1000);
    }

    // Only after the PIN: the web portal mirrors the screen and injects input, so bringing it
    // up any earlier would hand the lock screen to whoever is on the network.
    webPortalAutoStart();

    mainMenu(); // never returns
}

void loop() {}
