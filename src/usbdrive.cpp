#include "usbdrive.h"
#include "board.h"
#include "display.h"
#include "input.h"
#include "sdcard.h"
#include "serial_console.h"
#include "USB.h"
#include "USBMSC.h"
#include "esp32-hal-tinyusb.h" // usb_persist_restart
#include <SD.h>

USBCDC USBSerial;
static USBMSC msc;
static bool s_mscReady = false;

// MSC serves the SD card's raw 512-byte sectors straight from the SPI SD driver. Host
// transfers are sector-aligned, so `offset` is 0 and bufsize is a multiple of the sector
// size. The display is idle while USB drive mode is held, so the shared SPI bus is ours.
static int32_t mscRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    (void)offset;
    const uint32_t ss = SD.sectorSize();
    if (!ss) return -1;
    const uint32_t count = bufsize / ss;
    uint8_t *out = (uint8_t *)buffer;
    for (uint32_t i = 0; i < count; ++i)
        if (!SD.readRAW(out + i * ss, lba + i)) return -1;
    return (int32_t)bufsize;
}

static int32_t mscWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    (void)offset;
    const uint32_t ss = SD.sectorSize();
    if (!ss) return -1;
    const uint32_t count = bufsize / ss;
    for (uint32_t i = 0; i < count; ++i)
        if (!SD.writeRAW(buffer + i * ss, lba + i)) return -1;
    return (int32_t)bufsize;
}

static bool mscStartStop(uint8_t power_condition, bool start, bool load_eject) {
    (void)power_condition;
    (void)start;
    (void)load_eject;
    return true;
}

void usbSetup() {
    USBSerial.begin();
    USBSerial.setTxTimeoutMs(0); // never let a console write block the app if the host isn't reading
    if (sdInit()) {
        msc.vendorID("LilyGo");
        msc.productID("T-Embed SD");
        msc.productRevision("1.0");
        msc.onRead(mscRead);
        msc.onWrite(mscWrite);
        msc.onStartStop(mscStartStop);
        msc.mediaPresent(false); // appears as an empty reader until USB drive mode is entered
        if (msc.begin((uint32_t)SD.numSectors(), SD.sectorSize())) s_mscReady = true;
    }
    USB.begin();
}

bool usbDriveAvailable() { return s_mscReady; }

// While the PC has the card, nothing on this side may touch it. The Wi-Fi portal polls from
// the same input loop this screen uses, so without a flag to look at it would happily accept
// an upload and write to a filesystem the host believes it owns.
static bool s_handedToHost = false;

bool usbDriveActive() { return s_handedToHost; }

void usbDrivePresent(bool on) {
    s_handedToHost = on;
    if (s_mscReady) msc.mediaPresent(on);
}

void usbEnterDownloadMode() {
    delay(150);
    usb_persist_restart(RESTART_BOOTLOADER); // one-shot -> USB-Serial-JTAG download port
}

void usbDriveEnter() {
    if (!s_mscReady) {
        uiError("Insert SD, then reboot");
        delay(1800);
        return;
    }
    uiUsbScreen(0);
    usbDrivePresent(true); // the SD now shows up as a drive on the PC

    inputDrain();
    const uint32_t t0 = millis();
    for (;;) {
        if (inputPoll() == EV_BACK) break;
        const int frame = (int)((millis() - t0) / (uint32_t)uiCatDelayMs(CAT_ID_SLEEP));
        uiUsbScreen(frame); // big animated sleeping cat
        delay(30);
    }

    usbDrivePresent(false);
    delay(400);     // settle: let the host notice the eject + any transfer finish
    sdInvalidate(); // mark stale; re-scans lazily on the next Install (no SD.end on this path)
    inputDrain();
    // No uiMessage here: the first SPI touch right after the drive drops can wedge if an SD op
    // is still winding down. Just return — mainMenu redraws, which is the natural "done" signal.
}
