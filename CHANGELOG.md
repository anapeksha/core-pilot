# Changelog

All notable changes to Core Pilot are documented here. The project follows [Semantic Versioning](https://semver.org/) and [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

## [0.1.0] - 2026-11-04

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
- Applied JSON content-type validation consistently to `POST` and `PATCH` mutations.
- Corrected dashboard artifact collection in the signed-release workflow.

### Security

- Kept the Device Seed in dedicated internal flash.
- Disabled C++ exceptions and RTTI.
- Added bounded queues, request bodies, HTTP connections, records, and model collections.
- Prevented credentials, setup payloads, decrypted records, and key material from being logged.
- Required Core Pilot release signatures through ESP-IDF signed-app verification for application OTA updates.

[Unreleased]: https://github.com/anapeksha/core-pilot/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/anapeksha/core-pilot/releases/tag/v0.1.0
