#pragma once
#include "USBCDC.h"

// USB CDC serial (this project runs in USB-OTG/TinyUSB mode, so there is no CDC-on-boot
// Serial; the console uses this instead).
extern USBCDC USBSerial;

// Initialise USB: the CDC console and, if an SD card is present, an MSC "card reader"
// interface (media absent until USB drive mode is entered). Calls USB.begin(). Run once,
// after displayInit() (the SD needs the display's SPI bus).
void usbSetup();

// True if the MSC interface came up (an SD card was present at boot).
bool usbDriveAvailable();

// Enter USB drive mode: present the SD to the PC as a removable drive and hold a screen
// until the user presses BACK, then hide the drive again and re-read the card.
void usbDriveEnter();

// True while the SD card is handed to the PC. Anything on this side that writes to the card
// must stand down for the duration.
bool usbDriveActive();

// Low-level: show/hide the SD to the PC (used by the serial console for testing).
void usbDrivePresent(bool on);

// Reboot into the ROM USB download mode (one-shot; recovers after the next flash) so a
// browser web-flasher can reprogram the device with no buttons or PC tools.
void usbEnterDownloadMode();
