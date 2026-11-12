# Changelog

All notable changes to Core Pilot are documented here. The project follows [Semantic Versioning](https://semver.org/) and [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added

- A coordinated vulnerability disclosure policy using GitHub Private Vulnerability Reporting.
- Production stack-smashing protection, allocation-failure aborts, and FreeRTOS stack-overflow watchpoints, with automatic stack-variable initialization and light heap poisoning in development builds.
- Bounded raw TCP security probes for ambiguous headers, malformed framing, route normalization, pipelining, slow clients, and connection-slot recovery.

### Changed

- Disabled AI tools by default and disclosed when enabled read-only results leave the device for the configured provider.

### Fixed

- Required CoreS3 approval for every effectful MCP invocation path and reported proposed, queued, rejected, expired, and failed AI actions truthfully.
- Reclaimed timed-out Matter invoke callbacks without allowing stale or duplicate callbacks to complete reused slots.
- Isolated Wi-Fi scan generations across timeout, cancellation, and profile transitions.
- Applied strict duplicate-free JSON schemas and recursive secret cleanup to privileged mutation routes.
- Prevented source-table rate-limit exhaustion, preserved bootstrap availability under failed guesses, and reserved HTTP capacity from same-source slow clients.

### Security

- Kept physical bootstrap rotation local to the display while preserving active authenticated sessions.
- Rejected direct physical, persistent, and destructive MCP calls regardless of client-supplied arguments or confirmation text.

## [0.1.0-rc.3] - 2026-11-11

### Added

- Bounded Matter operation history and per-device subscription health in the authenticated API and dashboard.
- Deterministic release manifests with image digests, flash layout metadata, and application headroom checks.
- Global dashboard connectivity status with bounded recovery after the browser reconnects.

### Changed

- Updated GitHub Actions to their current friendly major versions.
- Added explicit dashboard logout, corrected session-duration guidance, and cleared cached device data when access ends.

## [0.1.0-rc.2] - 2026-11-11

### Added

- Physical CoreS3 approval for AI-proposed physical, persistent, and destructive actions.
- Dashboard proposal cards with local-approval guidance, effect labels, and expiry state.
- Bounded per-source HTTP request limiting with stricter authentication limits and retry guidance.
- Optional `HttpOnly`, same-site dashboard sessions while retaining bearer-token access for tooling.
- Live HTTP connection and dashboard transfer telemetry.
- Actionable, non-secret Matter commissioning failure categories.
- Guarded hardware runners for full Matter lifecycle and HTTP rate-limit recovery validation.
- Locked `uv` environment and portable CI job for host-side Python tools.

### Changed

- Moved forced local Matter record removal to the core-0 application owner.
- Reduced production BLE scanner verbosity while retaining detailed development diagnostics.
- Bound OTA chunks to opaque upload sessions and carried those sessions through the dashboard.
- Treated AI context and model tool calls as untrusted, with effectful calls held for independent local approval.

### Fixed

- Prevented a second unpair request from overwriting an unresolved late completion callback.
- Reconciled confirmed unpair completion with encrypted local registry state.
- Compared stress and soak recovery against pre-test reset state and transport-specific readiness.
- Quarantined timed-out Matter invoke callbacks and rejected late or duplicate completions safely.
- Rolled back Thread datasets and Wi-Fi profiles when live or encrypted persistence commits fail.
- Bounded Wi-Fi scans, HTTP transfers, and SMTP DNS resolution.
- Rejected duplicate JSON members and wiped sensitive parse trees on failure.

### Security

- Bound each AI approval to a short-lived, one-use proposal and the exact canonical tool arguments.
- Rejected fabricated assistant and tool history before it can influence local action execution.

### Known limitations

- Matter-over-Thread and Thread Border Router readiness have not yet been demonstrated on hardware.
- This release candidate is not CSA Matter certified and does not claim formal Matter specification compliance.

## [0.1.0-rc.1] - 2026-11-06

### Added

- ESP-IDF 6.0.2 C++26 firmware for M5Stack CoreS3 using M5Unified and M5GFX.
- Embedded ESP32-H2 OpenThread RCP with automatic CoreS3 UART provisioning.
- Embedded SolidJS dashboard with local `/api/v1` status and mutation endpoints.
- Device-bound encrypted SD storage with authenticated atomic record replacement.
- Saved station Wi-Fi startup and explicitly labeled setup-AP fallback.
- Boot progress, home, fatal, upgrade, screensaver, and low-power display views.
- Battery and external-power indication using CoreS3 PMIC readings.
- Encrypted room, group, scene, automation, and Matter endpoint persistence.
- Queue-owned Matter commissioning, endpoint discovery, and On/Off commands.
- ESP-Matter controller and OpenThread border-router integration for an H2 RCP.
- C++ formatting, Clang-Tidy, dashboard tests, browser tests, E2E tests, CI, and Zed configuration.
- Component-level logic contract tests plus native ASan/UBSan and ESP32-S3 Xtensa compile/link runners.
- Unified hardware stress, soak, and repeated Matter lifecycle validation with bounded recovery checks.
- Signed OTA firmware and a single merged initial-flash release image.

### Changed

- Simplified the CoreS3 home screen to show an actionable dashboard instruction.
- Pinned application storage work to core 0 and kept Matter work on core 1.
- Derived firmware versions from release tags instead of hardcoding the API version.
- Drained active subscriptions before unpairing and removed registry records only after successful completion.

### Fixed

- Preserved failed Matter owner-task operations and recovered subscriptions after asynchronous unpair failures.
- Correlated subscription callbacks with their originating lifecycle generation and bounded command completion deadlines.
- Reconciled successful unpair callbacks that arrive after an operation timeout.
- Applied and verified Thread datasets before encrypted persistence, with runtime rollback on application failure.
- Restored Matter subscriptions from the encrypted registry during reboot recovery.
- Reported Spinel, PASE, mDNS, IPv6, and Thread readiness only from observed runtime state.
- Applied JSON content-type validation consistently to `POST` and `PATCH` mutations.
- Corrected dashboard artifact collection in the signed-release workflow.

### Security

- Kept the Device Seed in dedicated internal flash.
- Disabled C++ exceptions and RTTI.
- Added bounded queues, request bodies, HTTP connections, records, and model collections.
- Prevented credentials, setup payloads, decrypted records, and key material from being logged.
- Required Core Pilot release signatures through ESP-IDF signed-app verification for application OTA updates.

### Known limitations

- Matter-over-Wi-Fi commissioning with the Nesso N1 currently fails before PASE and CASE become ready; complete commissioning, unpairing, recommissioning, and post-stress subscription recovery remain release-blocking hardware validations.
- Matter-over-Thread and Thread Border Router readiness have not yet been demonstrated on hardware.
- This release candidate is not CSA Matter certified and does not claim formal Matter specification compliance.

[Unreleased]: https://github.com/anapeksha/core-pilot/compare/v0.1.0-rc.3...HEAD
[0.1.0-rc.3]: https://github.com/anapeksha/core-pilot/compare/v0.1.0-rc.2...v0.1.0-rc.3
[0.1.0-rc.2]: https://github.com/anapeksha/core-pilot/compare/v0.1.0-rc.1...v0.1.0-rc.2
[0.1.0-rc.1]: https://github.com/anapeksha/core-pilot/releases/tag/v0.1.0-rc.1
