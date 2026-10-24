# Changelog

All notable changes to Core Pilot are documented here. The project follows [Semantic Versioning](https://semver.org/) and [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added

- ESP-IDF 5.5.6 C++20 firmware for M5Stack CoreS3 using M5Unified and M5GFX.
- Embedded SolidJS dashboard with local `/api/v1` status and mutation endpoints.
- Device-bound encrypted SD storage with authenticated atomic record replacement.
- Saved station Wi-Fi startup and explicitly labeled setup-AP fallback.
- Boot progress, home, fatal, upgrade, screensaver, and low-power display views.
- Battery and external-power indication using CoreS3 PMIC readings.
- Encrypted room, group, scene, automation, and Matter endpoint persistence.
- Queue-owned Matter commissioning, endpoint discovery, and On/Off commands.
- ESP-Matter controller and OpenThread border-router integration for an H2 RCP.
- C++ formatting, Clang-Tidy, dashboard tests, browser tests, E2E tests, CI, and Zed configuration.
- Unified hardware stress and soak validation with bounded recovery checks.
- Signed release firmware and complete initial-flash release bundles.

### Changed

- Simplified the CoreS3 home screen to show an actionable dashboard instruction.
- Pinned application storage work to core 0 and kept Matter work on core 1.
- Derived firmware versions from release tags instead of hardcoding the API version.

### Security

- Kept the Device Seed in dedicated internal flash.
- Disabled C++ exceptions and RTTI.
- Added bounded queues, request bodies, HTTP connections, records, and model collections.
- Prevented credentials, setup payloads, decrypted records, and key material from being logged.
- Required ESP-IDF publisher signatures for application OTA updates.
