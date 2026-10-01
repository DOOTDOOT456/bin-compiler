# bin-compiler

Compiles [WifiRepeater](sketch/WifiRepeater/WifiRepeater.ino) — an ESP8266 D1 Mini
sketch that auto-connects to any of up to 8 stored WiFi networks (whichever is in
range, strongest first) and hosts a browser-based config page for adding/removing
networks. Builds on every push to `main` via GitHub Actions; no local toolchain.

## Features

- **Multi-network auto-connect** — scans every 10s and jumps to the strongest
  stored network in range; reconnects automatically if the link drops.
- **Credentials survive reboot** — SSIDs + passwords stored in EEPROM.
- **Web config UI** — add/remove networks, live status, factory reset.
- **AP fallback** — always broadcasts `WifiRepeater-Setup` (open network) so you
  can reach the config page even when nothing is in range.

## Using the web UI

1. Power the board. From a phone/laptop, join the WiFi network **WifiRepeater-Setup**.
2. Browse to `http://192.168.4.1` (or the station IP shown on the serial monitor
   once connected).
3. Add your network(s) — the board saves them, disconnects, and immediately tries
   the strongest one in range.
4. Remove networks or factory-reset from the same page.

LED: **solid** = connected, **blinking** = scanning/AP mode.

## Build & flash

[![Compile sketch](https://github.com/DOOTDOOT456/bin-compiler/actions/workflows/compile.yml/badge.svg)](https://github.com/DOOTDOOT456/bin-compiler/actions/workflows/compile.yml)

1. Download the **WifiRepeater** artifact from the latest green run.
2. Put the D1 Mini in bootloader mode (hold **FLASH**, tap **RST**).
3. Open [esptool.spacehuhn.com](https://esptool.spacehuhn.com) in Chrome/Edge,
   connect, add `WifiRepeater.ino.d1_mini.bin` at flash address **`0x0`**, program.
4. Tap **RST** to boot. Join `WifiRepeater-Setup` and configure it.

> The ESP8266 Arduino image is self-contained — do not add bootloader/partition files.
