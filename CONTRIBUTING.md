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

## Making changes

- Follow the existing style and use the simplest solution that addresses the root cause.
- Preserve the local-first design: normal controls and automations must not require cloud or AI services.
- Keep inputs, queues, retries, and stored collections bounded.
- Preserve encryption and authentication for mutable data.
- Do not claim that hardware or protocol behavior works unless it was validated on the relevant hardware.
- Do not expose the dashboard to the internet or add automatic port forwarding, cloud relay, or an open setup network.
- Keep user-visible text clear and action-oriented.
- Add or update tests when behavior changes.

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

Hardware-dependent changes require testing on a positively identified device. Record the device, action, expected result, observed result, and pass/fail status. Redact all sensitive values from logs and screenshots.

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

Do not commit release artifacts, build trees, secrets, or local configuration.

## Reporting security issues

Do not publish an exploitable security issue, secret, or sensitive device data in a public issue. Contact the maintainers privately through the repository owner's security contact or GitHub's private vulnerability reporting feature when available. Include only the minimum redacted information needed to reproduce the problem.
