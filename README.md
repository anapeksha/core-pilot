# Core Pilot

Core Pilot is a local-first smart-home controller for M5Stack CoreS3. It combines an ESP-IDF C++20 firmware with an embedded SolidJS dashboard and keeps mutable application data encrypted on the device's SD card.

## Features

- M5Unified board, touch, RTC, power, and display integration.
- M5GFX canvas rendering, boot progress, screensaver, and low-power display mode.
- Saved Wi-Fi station startup with an explicitly labeled setup-AP fallback.
- Embedded, hash-routed SolidJS dashboard served over the trusted local network.
- Device-bound encrypted SD records with authenticated atomic replacement.
- Encrypted room, group, scene, automation, and discovered Matter endpoint storage.
- Event-driven Matter controller queue for commissioning, endpoint discovery, and device commands.
- ESP-Matter commissioner and OpenThread border-router integration for the Gateway H2 RCP.
- Bounded HTTP connections, request bodies, queues, records, and model collections.

Matter-over-Wi-Fi and Matter-over-Thread remain marked unavailable until their complete commissioning and reboot-recovery paths have passed hardware validation. Unsupported services do not simulate readiness.

## Repository

- `firmware/` — ESP-IDF 5.5.6 C++20 firmware.
- `web/` — SolidJS dashboard embedded as `dist/index.html.gz`.
- `.github/` — firmware and dashboard CI.

## Build

Install ESP-IDF 5.5.6 with EIM and build the dashboard before the firmware:

```sh
cd web
npm ci
npm run build

cd ../firmware
eim run 'idf.py -B build build' v5.5.6
```

## Flash

Confirm the port before writing firmware:

```sh
cd firmware
eim run 'idf.py -B build -p /dev/cu.usbmodem1201 flash' v5.5.6
```

The normal command asks esptool to reset the ESP32-S3 into its ROM downloader automatically. CoreS3 units whose USB serial control lines do not support that reset can be flashed directly through the built-in USB-JTAG interface without entering download mode:

```sh
eim run 'python ../tools/flash_jtag.py build' v5.5.6
```

Both methods verify the written image. `probe-rs` is not required; ESP-IDF's OpenOCD supports the CoreS3 USB-JTAG interface directly.

## Checks

```sh
python tools/format.py
eim run 'idf.py -C firmware -B firmware/build-clang -D IDF_TOOLCHAIN=clang reconfigure' v5.5.6
python tools/lint.py build-clang
eim run 'idf.py -C firmware -B firmware/build build' v5.5.6

cd web
npm run format
npm run lint
npm run check
npm run test
npm run test:coverage
npm run test:browser
npm run build
npm run test:e2e
```

C++ exceptions and RTTI are disabled. Security-sensitive buffers are bounded and explicitly zeroized. See `AGENTS.md` for architecture and hardware constraints and `CHANGELOG.md` for release history.
