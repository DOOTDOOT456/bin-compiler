# bin-compiler

Compiles the [BlinkD1Mini](sketch/BlinkD1Mini/BlinkD1Mini.ino) sketch for the
WeMos D1 Mini (ESP8266) on every push to `main` using GitHub Actions — no local
toolchain required.

## Build

[![Compile sketch](https://github.com/DOOTDOOT456/bin-compiler/actions/workflows/compile.yml/badge.svg)](https://github.com/DOOTDOOT456/bin-compiler/actions/workflows/compile.yml)

1. Open the [Actions tab](https://github.com/DOOTDOOT456/bin-compiler/actions/workflows/compile.yml)
   and pick the latest green **Compile sketch** run.
2. Download the **BlinkD1Mini** artifact — you get `BlinkD1Mini.ino.d1_mini.bin`.

The build uses [`arduino/compile-sketches`](https://github.com/arduino/compile-sketches)
with FQBN `esp8266:esp8266:d1_mini` and the ESP8266 package index
(`https://arduino.esp8266.com/stable/package_esp8266com_index.json`).

## Flash

1. Open [esptool.spacehuhn.com](https://esptool.spacehuhn.com) (or esptool-js) in
   a Chromium-based browser.
2. Connect the D1 Mini in bootloader mode (hold **FLASH** while tapping
   **RST**), then pick the serial port.
3. Click **Add file** and select `BlinkD1Mini.ino.d1_mini.bin`.
4. Set the flash address to **`0x0`**.
5. Click **Program** and wait for the confirmation.

> The ESP8266 Arduino image is self-contained: bootloader and partition data are
> baked into the binary, so do **not** add separate bootloader/partition files.

After flashing, reset the board — the onboard LED (GPIO2 / D4, active LOW)
should blink once per second.
