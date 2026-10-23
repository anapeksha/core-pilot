## Summary

<!-- What changed, and why is this the simplest appropriate solution? -->

## Scope

- [ ] Firmware
- [ ] Dashboard
- [ ] Storage or security
- [ ] Matter or Thread
- [ ] OTA or recovery
- [ ] Documentation or CI only

## Validation

<!-- List the exact commands and hardware/network/provider checks actually run. Do not claim hardware validation from mocks or host tests. -->

- [ ] `python tools/format.py`
- [ ] `python tools/lint.py build-clang`
- [ ] `idf.py build`
- [ ] Dashboard format, lint, type-check, tests, build, and E2E
- [ ] Hardware validation recorded below, or not applicable

### Hardware validation

<!-- If applicable, record the device, exact action, expected behavior, observed behavior, and pass/fail. Otherwise write "Not applicable." -->

## Safety checklist

- [ ] No secret, Device Seed, credential, or private key is logged, exposed, or committed.
- [ ] Mutable SD data remains authenticated and encrypted through `StorageService`.
- [ ] Queues, bodies, retries, actions, and retained records remain bounded.
- [ ] Local controls and automations do not depend on AI.
- [ ] Unsupported Matter, Thread, OTA, provider, or hardware behavior is not presented as validated.
- [ ] No WAN exposure, UPnP, NAT-PMP, cloud relay, or dashboard authentication was added unintentionally.

## Screenshots or logs

<!-- Add bounded, redacted evidence when it helps review. -->

## Follow-up work

<!-- Note intentionally deferred work, migrations, or operator actions. -->
