# Core Pilot engineering guide

## Product

Core Pilot is a local-first smart-home controller for M5Stack CoreS3. `firmware/` is an ESP-IDF 5.5.6 C++20 application. `web/` is a static SolidJS dashboard embedded into the firmware image.

## Ownership

- M5Unified owns CoreS3 peripherals; M5GFX/M5Canvas owns rendering.
- `core_board` exclusively coordinates the LCD/TF shared SPI2 bus and GPIO35 handoff.
- `core_display` is the only application component that accesses display, touch, RTC, or power APIs.
- `core_security` owns Device Seed use, key derivation, authenticated encryption, and zeroization.
- `core_storage` owns encrypted SD records and atomic replacement.
- `core_network` owns Wi-Fi and mDNS lifecycle.
- `core_matter` owns Matter controller operations and OpenThread border routing.

Component names retain the `core_` prefix to avoid collisions with ESP-IDF and third-party components. C++ application types remain in the `core_pilot` namespace.

## Concurrency

- UI, HTTP orchestration, storage, and ordinary application work run on core 0.
- Matter and application-owned Thread work run on core 1.
- Each protocol stack has one owner task.
- Cross-core requests use bounded FreeRTOS queues containing trivially-copyable value structs.
- HTTP callbacks enqueue work and return operation state; they must not mutate Matter, Thread, OTA, or storage state concurrently.
- Do not add polling loops when an ESP-IDF event or bounded queue can express the same operation.

## Hardware

- CoreS3 LCD and TF share SPI2: SCLK GPIO36, MOSI GPIO37, MISO/LCD D-C GPIO35, LCD CS GPIO3, TF CS GPIO4.
- LCD CS and TF CS must never be active together. Restore GPIO35 to SPI2 MISO/input after LCD traffic.
- CoreS3 has 16 MiB flash and 8 MiB quad PSRAM.
- CoreS3 serial is `/dev/cu.usbmodem1201`.
- If there are other serial devices, make sure to never flash Core Pilot firmware to it.
- Gateway H2 uses CoreS3 RX GPIO10, TX GPIO17, EN GPIO7, BOOT GPIO18, at 460800 baud.

## Security

- Never export, log, display, transmit, or place the Device Seed on SD.
- Never persist mutable application records as plaintext.
- Never log credentials, setup payloads, decrypted records, private keys, or tokens.
- Zero temporary key and credential buffers immediately after use.
- Reject malformed, oversized, unsupported, or unauthenticated input.
- Keep exceptions and RTTI disabled. Prefer RAII, fixed bounds, explicit status values, and single ownership.

## APIs and readiness

- Preserve `/api/v1` contracts consumed by `web/`.
- API responses use `Cache-Control: no-store` and `Connection: close`.
- Station mode shows the joined SSID and DHCP URL. Setup AP mode is explicitly labeled and uses `192.168.4.1`.
- Never report Matter, Thread, OTA, SMTP, AI, or time services as ready before real implementation and validation.
- Do not enable WAN exposure, NAT, UPnP, NAT-PMP, or cloud relay by default.

## Validation

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

Hardware claims require captured device evidence. Matter-over-Wi-Fi requires PASE, attestation, AddNOC, CASE, subscriptions, commands, and reboot recovery. Matter-over-Thread additionally requires H2 Spinel startup, an active dataset, attachment, routed IPv6, and DNS-SD.

Do not commit unless the user explicitly asks. When requested, use small Conventional Commits and never commit secrets or generated build trees.
