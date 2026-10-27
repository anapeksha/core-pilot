# Core Pilot engineering guide

## Product

Core Pilot is a local-first smart-home controller for M5Stack CoreS3. `firmware/s3/` is an ESP-IDF 6.0.2 C++26 application, and `firmware/h2/` is its embedded ESP32-H2 OpenThread RCP. `web/` is a static SolidJS dashboard embedded into the firmware image.

## Ownership

- M5Unified owns CoreS3 peripherals; M5GFX/M5Canvas owns rendering.
- `core_board` exclusively coordinates the LCD/TF shared SPI2 bus and GPIO35 handoff.
- `core_display` is the only application component that accesses display, touch, RTC, or power APIs.
- `core_security` owns Device Seed use, key derivation, authenticated encryption, and zeroization.
- `core_storage` owns encrypted SD records and atomic replacement.
- `core_network` owns Wi-Fi and mDNS lifecycle.
- `core_matter` owns Matter controller operations and OpenThread border routing.
- `core_gateway` owns embedded ESP32-H2 RCP validation, UART provisioning, and reset sequencing.
- `core_ota` owns signed image staging, digest/signature/version verification, boot-partition activation, and deferred restart. The core-0 application task owns OTA display transitions.

Component names retain the `core_` prefix to avoid collisions with ESP-IDF and third-party components. C++ application types remain in the `core_pilot` namespace.

## Concurrency

- UI, HTTP orchestration, storage, and ordinary application work run on core 0.
- Matter and application-owned Thread work run on core 1.
- Each protocol stack has one owner task.
- Cross-core requests use bounded FreeRTOS queues containing trivially-copyable value structs.
- HTTP callbacks enqueue work and return operation state; they must not mutate Matter, Thread, OTA, or storage state concurrently.
- Matter commissioning, endpoint commands, discovery, subscriptions, and unpairing run through the bounded core-1 owner queue. Remove encrypted registry entries only after the unpair callback succeeds.
- Automations, OTA, Wi-Fi provisioning, AI, and SMTP use bounded core-0 worker queues or ESP-IDF events. Do not add an internal MQTT broker to duplicate these queues.
- Do not add polling loops when an ESP-IDF event or bounded queue can express the same operation. The existing application-loop reconciliation of completed owner-task operations is intentional and bounded.

## Hardware

- CoreS3 LCD and TF share SPI2: SCLK GPIO36, MOSI GPIO37, MISO/LCD D-C GPIO35, LCD CS GPIO3, TF CS GPIO4.
- LCD CS and TF CS must never be active together. Restore GPIO35 to SPI2 MISO/input after LCD traffic.
- CoreS3 has 16 MiB flash and 8 MiB quad PSRAM.
- Discover or confirm the current CoreS3 DHCP address before relying on it.
- If there are other serial devices, identify the CoreS3 positively before any flash command.
- Gateway H2 uses CoreS3 RX GPIO10, TX GPIO17, EN GPIO7, BOOT GPIO18, at 460800 baud.

## Security

- Never export, log, display, transmit, or place the Device Seed on SD.
- Never persist mutable application records as plaintext.
- Never log credentials, setup payloads, decrypted records, private keys, or tokens.
- Zero temporary key and credential buffers immediately after use.
- Reject malformed, oversized, unsupported, or unauthenticated input.
- Persisted model limits are UTF-8 byte limits, not character counts: IDs 48 bytes, names 64 bytes, details 96 bytes, group members 32, scene/automation actions 8, and local notifications 72 bytes. Enforce matching limits in UI forms, web request schemas, firmware request parsing, and persistence codecs.
- Matter node IDs accepted by HTTP must be nonzero hexadecimal values of at most 16 digits; reject signs, prefixes, overflow, and non-hex characters.
- Keep exceptions and RTTI disabled. Prefer RAII, fixed bounds, explicit status values, and single ownership.

## APIs and readiness

- Preserve `/api/v1` contracts consumed by `web/`. Matter device removal is `POST /api/v1/matter/devices/remove` with `{ "device_id": "<hex node id>" }`; it queues unpairing and must not synchronously delete registry state.
- The server uses ESP-IDF `esp_http_server`: register bounded `httpd_uri_t` handlers, use `httpd_resp_send_err` for HTTP/parser failures, and use structured JSON mutation responses for asynchronous application acceptance or rejection.
- API responses use `Cache-Control: no-store` and `Connection: close`.
- Station mode shows the joined SSID and DHCP URL. Setup AP mode is explicitly labeled and uses `192.168.4.1`.
- Never report Matter, Thread, OTA, SMTP, AI, or time services as ready before real implementation and validation.
- Show the LCD firmware-upgrade screen only after OTA digest, signature, embedded version, and boot-partition activation all succeed. Failed uploads leave the normal display unchanged.
- Keep dashboard access on HTTP until there is a real per-device certificate enrollment, hostname/SAN, renewal, and recovery design. A shared or silently generated self-signed certificate is not an acceptable way to claim browser camera secure-context support.
- Do not enable WAN exposure, NAT, UPnP, NAT-PMP, or cloud relay by default.

## Validation

Run formatting and builds sequentially; `tools/format.py` may remove generated dashboard artifacts while `npm run build` is writing them.

```sh
python tools/format.py
eim run 'idf.py -C firmware/s3 -B firmware/s3/build-clang -D IDF_TOOLCHAIN=clang reconfigure' v6.0.2
python tools/lint.py build-clang
eim run 'idf.py -C firmware/s3 -B firmware/s3/build build' v6.0.2

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

Use the hardware runner only against a positively identified CoreS3 address. The HTTP server has three active connection slots; three stress workers passed 500/500 requests, while eight workers intentionally exceeded admission capacity.

```sh
python tools/test.py stress <core-s3-ip> --stress-requests 500 --stress-workers 3 --timeout 5 --max-failure-rate 0.01
python tools/test.py both <core-s3-ip> --duration-hours 8 --sample-interval 30 --sample-attempts 3 --stress-interval 300 --stress-requests 200 --stress-workers 3 --timeout 5 --max-failure-rate 0.01
```

Do not start or claim a soak pass unless its initial sample has Matter, Thread, CASE, and subscriptions ready. Hardware claims require captured device evidence. Matter-over-Wi-Fi requires PASE, attestation, AddNOC, CASE, subscriptions, commands, and reboot recovery. Matter-over-Thread additionally requires H2 Spinel startup, an active dataset, attachment, routed IPv6, and DNS-SD.

## Current hardware test state

- CoreS3 `0.1.0-ota-test3` passed 500/500 HTTP requests with three workers and recovered from a requested reboot with software reset reason `3`.
- Nesso node `000296ea175bcd1f` was successfully unpaired and removed from the encrypted registry. Recommissioning then failed reproducibly in PASE after the 180-second timeout.
- Nesso reports that CHIPoBLE commissioning is open but also reports failures advertising the commissionable node and finalizing DNS-SD service updates. Until that accessory-side advertising path is repaired and Nesso is recommissioned, CASE and subscription readiness are false and an eight-hour protocol-ready soak cannot pass.
- Never weaken the soak readiness checks or claim success to hide this known hardware failure.

Do not commit unless the user explicitly asks. When requested, use small Conventional Commits and never commit secrets or generated build trees.
