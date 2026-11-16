# Contributing to Core Pilot

Thank you for helping improve Core Pilot. Contributions may include bug fixes, dashboard improvements, firmware features, tests, documentation, and hardware-validation results.

## Before you start

- Search existing issues and pull requests to avoid duplicating work.
- For a substantial feature or architecture change, open an issue first so the approach can be discussed.
- Keep changes focused. Avoid unrelated cleanup in the same pull request.
- Never include credentials, access codes, session tokens, Matter setup payloads, Device Seeds, private keys, decrypted records, or generated build directories.

## Development setup

Core Pilot has two main parts:

- `firmware/s3/`: an ESP-IDF 6.0.2 application for M5Stack CoreS3.
- `firmware/h2/`: an ESP-IDF 6.0.2 ESP32-H2 OpenThread RCP embedded by the CoreS3 build.
- `web/`: the SolidJS dashboard embedded in the firmware image.

Install ESP-IDF 6.0.2 through [EIM](https://github.com/espressif/idf-env), Node.js, and npm. Install dashboard dependencies with:

```sh
cd web
npm ci
```

Build the dashboard before building firmware:

```sh
cd web
npm run build

cd ../firmware/s3
eim run 'idf.py -B build build' v6.0.2
```

See [`README.md`](README.md) for installation, flashing, and first-run instructions.

### C/C++ editor tooling

Install ESP-Clang through EIM for ESP-IDF 6.0.2, and ensure `eim` is on the editor's PATH. From the repository root, generate the target-specific compilation databases:

```sh
python3 tools/configure_clangd.py
```

Repeat after moving the checkout/toolchain or changing component dependencies: CMake databases contain absolute paths. The script exports the native host database at `firmware/s3/test/host/build`, configures production S3 and H2 `build-clang/` trees (including H2's recipe-derived manifest), configures only project-owned component/main tests at `firmware/s3/build-tests-clang`, and configures the standalone runner at `firmware/s3/test/esp32s3/build-clang`. More-specific `.clangd` rules select runner/test databases without changing production source commands. CMake, Ninja, and a native C++23 compiler are required for the host configuration. It does not flash hardware or enable development authentication; S3 configuration does invoke the existing embedded H2 build. Keep `build/` for the production GCC build.

Every Clang configuration explicitly sets an absolute `SDKCONFIG` path to `sdkconfig` inside its own ignored build directory. Missing files are generated from the project's normal defaults; subsequent runs reuse that build-local configuration. Do not point Clang configurations at the project-level production `sdkconfig`: Clang's Picolibc selection can otherwise leak into GCC builds and cause TLS `errno` link failures. No production security defaults are changed.

Zed uses `tools/clangd.py` to activate the pinned EIM environment and launch Espressif's clangd, not Apple/host clangd. EIM logging goes to the platform null device and tracking is disabled; activation output never enters the LSP stream. The launcher permits querying ESP-Clang and explicitly listed `/usr/bin` native drivers for target/system includes. Restart the C/C++ language server after changing setup. Other editors can use the same launcher with `--enable-config`. Do not hardcode SDK include paths or suppress diagnostics to accommodate a host toolchain.

For a compiler-diagnostic smoke check (the whole translation unit is parsed; the line range only limits clangd's refactoring self-tests):

```sh
tools/clangd.py --enable-config --check=firmware/s3/components/core_board/src/core_board.cpp --check-lines=1-1
tools/clangd.py --enable-config --check=firmware/s3/components/core_display/src/core_display.cpp --check-lines=1-1
tools/clangd.py --enable-config --check=firmware/h2/main/esp_ot_rcp.c --check-lines=1-1
```

ESP-Clang 20.1.1 may be incompatible with newer native macOS SDK headers even after native driver discovery (`__darwin_wint_t` was observed with macOS SDK 27.2). Treat that as a host tooling compatibility failure, not permission to suppress diagnostics or modify firmware headers. H2 currently also has an existing `modernize-use-nullptr` clang-tidy finding in C source.

Unrestricted `--check` may additionally report overlapping macro replacements in clangd's `SwapBinaryOperands` tweak self-tests; these are not firmware compiler errors. Clang-tidy findings remain enabled and are reported separately from compiler failures.

### Development-only authentication bypass

A local hardware-development build may bypass bearer authentication when repeated API testing would otherwise require entering physical access codes. This mode is deliberately opt-in and must only be used on a physically controlled device and trusted local network.

Build it in a dedicated directory with the checked-in defaults overlay:

```sh
eim run 'idf.py -C firmware/s3 -B firmware/s3/build-development -D PROJECT_VER=0.1.4-dev -D SDKCONFIG=sdkconfig.development.generated -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.development;sdkconfig.development.defaults" reconfigure build' v6.0.2
```

The development image reports `authentication_required:false` from `/api/v1/auth` and emits a prominent boot warning. Host, Origin, content-type, setup-mode, and request validation remain active.

Rules for this mode:

- Never add the bypass to `sdkconfig.defaults` or a release workflow.
- Never commit the generated `firmware/s3/sdkconfig.development.generated` file or `build-development/` tree. The tracked `sdkconfig.development` file is an input overlay that disables release signing for directly flashed development applications; do not overwrite it.
- Never use a development image on an untrusted network or distribute it as firmware.
- Never use production signing keys for a development image.
- Restore a CI-built, production-signed image and verify protected requests return `401` without a bearer token before reporting hardware validation complete.
- CI and release builds use `sdkconfig.defaults` only, where the bypass defaults to disabled.

## Making changes

- Follow the existing style and use the simplest solution that addresses the root cause.
- Preserve the local-first design: normal controls and automations must not require cloud or AI services.
- Keep inputs, queues, retries, and stored collections bounded.
- Preserve encryption and authentication for mutable data.
- Do not claim that hardware or protocol behavior works unless it was validated on the relevant hardware.
- Do not expose the dashboard to the internet or add automatic port forwarding, cloud relay, or an open setup network.
- Keep user-visible text clear and action-oriented.
- Add or update tests when behavior changes.

Dashboard loading, retry, URL-filter, editor, and accessibility behavior is documented in [`docs/dashboard.md`](docs/dashboard.md).

The detailed ownership, concurrency, hardware, and security rules for the project are documented in [`AGENTS.md`](AGENTS.md). Contributors changing firmware should read it before editing component boundaries or hardware access.

## Validation

Run the checks related to your changes. For a complete validation pass, run these commands sequentially from the repository root:

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

Do not run firmware formatting and the dashboard build concurrently because formatting may remove generated dashboard artifacts while they are being written.

### Portable firmware logic tests

CI has two deliberately separate test environments for firmware logic. Component-owned Unity cases remain beside their production components under `firmware/s3/components/core_*/test/`. The projects under `firmware/s3/test/` are runners: they assemble those policies for a specific execution environment without becoming part of the production firmware image.

1. **Native host execution** configures `firmware/s3/test/host/` with the host compiler. Three sanitizer-backed executables run the existing logic checks, the real authorization implementation, and the shared component-owned cases for board geometry, display, gateway verification, Matter lifecycle/history, networking, and OTA. Cross-component cases exercise the production authorization-to-display mapping together with touch admission, code exchange, and reboot recovery. Missing required suites fail the runner; each executable has a 30-second limit:

   ```sh
   cmake -S firmware/s3/test/host -B firmware/s3/test/host/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
   cmake --build firmware/s3/test/host/build
   ctest --test-dir firmware/s3/test/host/build --output-on-failure --no-tests=error
   ```

2. **ESP32-S3 compile/link validation** builds `firmware/s3/test/esp32s3/` with the official ESP-IDF 6.0.2 toolchain. The image reuses the existing component Unity test sources and verifies that they compile and link for the actual `esp32s3` target:

   ```sh
   eim run 'idf.py -C firmware/s3/test/esp32s3 -B firmware/s3/test/esp32s3/build -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32s3' v6.0.2
   eim run 'idf.py -C firmware/s3/test/esp32s3 -B firmware/s3/test/esp32s3/build build' v6.0.2
   ```

The second layer is labeled **compile/link only** in CI: GitHub-hosted runners do not execute the image. The built binaries and ELF are uploaded as a CI artifact. Its startup checks test registration before opening the Unity menu. The standalone runner has a different flash layout from the application; do not flash it over an installed controller without an explicit backup/recovery plan.

There is currently no ESP32-S3 QEMU execution job. Espressif's QEMU fork supports ESP32-S3 CPU/system emulation, so the minimal Unity image may be a future candidate for deterministic execution after its boot and test-runner behavior is proven reliable in CI. QEMU does not model the CoreS3 board peripherals or provide hardware evidence for Wi-Fi/BLE, the H2 RCP, Matter/Thread integration, timing, LCD/TF bus ownership, storage, touch, RTC, or OTA behavior. Native host execution and QEMU therefore complement—but do not replace—real target validation.

These test builds use normal test configuration and do not use the development authentication overlay.

Hardware-dependent changes require testing on a positively identified device. Record the device, action, expected result, observed result, and pass/fail status. Redact all sensitive values from logs and screenshots.

Host-side tools use the locked, dependency-free `uv` project under `tools/`:

```sh
uv run --project tools --locked python -m unittest discover -s tools/tests -p 'test_*.py' -v
```

Matter lifecycle validation is destructive and runs only against a development-auth build. Select the currently registered accessory explicitly and keep its setup payload in process memory; never put either value in repository-wide documentation or logs:

```sh
export MATTER_SETUP_CODE='<accessory setup payload>'
export CORE_PILOT_MATTER_NODE_ID='<current node id>'
export CORE_PILOT_BEARER='<RAM session token>'
uv run --project tools --locked python tools/test.py lifecycle <core-s3-ip> --confirm-destructive --lifecycle-cycles 3 --matter-transport wifi
```

Each cycle verifies an idempotent command, reboot and subscription recovery, another command, asynchronous unpairing, encrypted registry removal, recommissioning, CASE, and subscriptions. Validate the limiter contract separately with `uv run --project tools --locked python tools/test.py rate-limit <core-s3-ip>`; this intentionally exhausts the source bucket and waits for the advertised retry interval before checking recovery.

### Browser and production boot regressions

Playwright runs against the built dashboard with simulated firmware responses. Its cases cover explicit physical-authorization guidance, independent browser sessions, reboot/session expiry, failed requests, cached-data clearing, queued/rejected mutations, OTA rejection, UTF-8 limits, and narrow-screen keyboard access. They do not establish real Matter behavior. CI rejects focused tests and retains failure traces/screenshots; the firmware build waits for Python, browser, native, and S3 compile/link checks to pass.

After flashing a signed production image to a positively identified CoreS3, capture a complete boot and confirm its current DHCP address. Keep captures in ignored local scratch space and redact secrets. Check the boot and unauthenticated API with:

```sh
uv run --project tools --locked python tools/boot_check.py <fresh-boot-capture> <current-core-s3-ip> --version 0.1.4
```

This bounded smoke check requires exactly one boot, PSRAM/storage self-test completion, the expected version/address, and application-loop entry. It rejects panic/allocation failures and startup memory below 16 KiB internal free, 8 KiB largest internal block, or 8 KiB DMA-capable free. These are regression floors, not proof of workload headroom. It checks idle production authentication, unauthenticated/invalid-cookie rejection, and that an exchange before a physical tap cannot issue a session or code. It never uses an environment bearer token and stops before exchanging if a code is already open.

Boot/API success is not a stress, soak, commissioning, or peripheral-validation result. Physical touchscreen, signed OTA recovery, accessory interoperability, and peak memory workloads still require separate hardware evidence. Do not program eFuses during testing.

## Releases

Update the firmware/dashboard versions and add the release entry to `CHANGELOG.md` before tagging a release. Use only that version's changelog body as the GitHub release text, preserving its section headings; do not copy other versions or append PR descriptions and validation logs. Wait for CI and CodeQL before merging, then use the production release workflow to sign and verify the published images. Never publish development authentication-bypass builds or program eFuses as part of a release.

## Pull requests

A pull request should:

1. Explain what changed and why.
2. Stay limited to one coherent concern where practical.
3. Include tests or explain why tests are not applicable.
4. List the exact validation commands that were run.
5. Distinguish simulated or host testing from real hardware validation.
6. Include screenshots for meaningful visual changes when possible.
7. Call out behavior changes, limitations, and follow-up work.

Use the repository pull-request template and ensure continuous integration passes. Maintainers may request a smaller change set or additional hardware evidence before merging.

## Commit messages

Prefer small commits using the Conventional Commits style, for example:

```text
feat: add room favorites
fix: keep browser sessions independent
docs: explain first-time setup
test: cover expired access codes
```

Do not commit release artifacts, build trees, secrets, generated `sdkconfig` files, or local configuration.

## Reporting security issues

Do not publish an exploitable security issue, secret, or sensitive device data in a public issue. Contact the maintainers privately through the repository owner's security contact or GitHub's private vulnerability reporting feature when available. Include only the minimum redacted information needed to reproduce the problem.
