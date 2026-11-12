#!/usr/bin/env python3
"""Flashes launcher.bin, handling the USB-OTG port dance.

In USB-OTG mode the running launcher shows up as an Espressif TinyUSB CDC port
(VID 303A / PID 0002). Resetting into the ROM download mode switches the USB over to the
USB-Serial-JTAG port (PID 1001), so esptool must flash on THAT port. This script:

  1. finds the OTG port and pokes it to drop into download mode,
  2. waits for the JTAG (1001) port to appear,
  3. runs esptool write_flash there.

If the device is already in download mode (e.g. you held BOOT while resetting), it just
flashes. Usage:  python tools/flash.py [firmware/launcher.bin]
"""
import os
import subprocess
import sys
import time

from serial.tools import list_ports

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ESPTOOL = [sys.executable, "-m", "esptool"]


def find(pid):
    """The Espressif port with this PID. Only VID 0x303A is considered, so unrelated USB
    serial adapters (CH340, Seeed, ...) are never touched. If several Espressif devices are
    attached, refuse to guess — pass the port explicitly instead."""
    hits = [p.device for p in list_ports.comports() if p.vid == 0x303A and p.pid == pid]
    if len(hits) > 1:
        sys.exit("Multiple Espressif devices on %s: %s\nRun: python tools/flash.py --port COMx"
                 % (hex(pid), ", ".join(hits)))
    return hits[0] if hits else None


def main():
    # Flash the pieces separately and SKIP the NVS region (0x9000) + otadata (0xE000), so
    # the PIN and app names survive an update. (First-time installs use erase + everything.)
    build = os.path.join(HERE, ".pio", "build", "lilygo-t-embed-cc1101")
    parts = [
        ("0x0", os.path.join(HERE, "bootloader", "bootloader.bin")),
        ("0x8000", os.path.join(build, "partitions.bin")),
        ("0x10000", os.path.join(build, "firmware.bin")),
    ]
    for _, f in parts:
        if not os.path.exists(f):
            sys.exit("Missing %s (run `pio run` first)" % f)

    # An explicit "--port COMx" overrides all detection (use it when several boards are attached).
    forced = None
    if "--port" in sys.argv:
        i = sys.argv.index("--port")
        if i + 1 < len(sys.argv):
            forced = sys.argv[i + 1]

    jtag = forced or find(0x1001)
    if not jtag:
        otg = find(0x0002)
        if otg:
            print("Launcher on %s (OTG) -> poking into download mode ..." % otg)
            try:
                subprocess.run(ESPTOOL + ["--chip", "esp32s3", "--port", otg, "read-mac"],
                               capture_output=True, timeout=15)
            except Exception:
                pass
        for _ in range(70):  # Windows can be slow to attach the JTAG driver after the reset
            time.sleep(0.5)
            jtag = find(0x1001)
            if jtag:
                time.sleep(1.0)  # let the port settle before opening
                break
    if not jtag:
        sys.exit("No download (JTAG, PID 1001) port found.\n"
                 "Hold the BOOT button, tap RESET, then run this again.")

    print("Flashing on %s (preserving NVS) ..." % jtag)
    env = dict(os.environ, PYTHONIOENCODING="utf-8", PYTHONUTF8="1")
    wf = []
    for off, f in parts:
        wf += [off, f]

    # esptool renamed its subcommands between v4 and v5 ("write_flash" -> "write-flash",
    # "hard_reset" -> "hard-reset"). Which one is installed depends on the PlatformIO
    # packages of the day, so ask rather than assume.
    v5 = True
    try:
        out = subprocess.run(ESPTOOL + ["version"], capture_output=True, text=True, timeout=30)
        v5 = not (out.stdout + out.stderr).strip().lstrip("esptool.py v").startswith("4.")
    except Exception:
        pass
    write, reset = ("write-flash", "hard-reset") if v5 else ("write_flash", "hard_reset")

    r = subprocess.run(ESPTOOL + ["--chip", "esp32s3", "--port", jtag, "--baud", "921600",
                                  "--after", reset, write] + wf, env=env)
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
