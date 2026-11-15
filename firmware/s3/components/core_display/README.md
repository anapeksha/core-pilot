# Core Pilot display component

`CoreDisplay` owns M5Unified initialization and all M5 display, touch, PMIC and RTC access. Call `begin()` once, then `poll()` from the **same task** every 20–50 ms. Call `bootSplash()`, `bootProgress()`, `fatal()`, `home()`, and `upgrade()` from that task only. The caller supplies real network/status lines and OTA byte counts; this component never invents service readiness or upgrade progress. A fatal page stays visible until explicitly replaced. Upgrade and fatal pages do not enter idle sleep. `home()` resets the idle timer; contact wakes the home page (not an upgrade/fatal page). Do not initialize or poll M5 separately in the application.

The application uses this component as the sole M5 owner. Synchronized system time is preferred, with `M5.Rtc` as the fallback. Until either source is valid, home shows `--:--` and the screensaver reports that time is synchronizing. The component uses a PSRAM-backed 320×240 RGB565 canvas, a 50×24 time sprite, and a 32×14 battery sprite. Screen transitions and the screensaver push the full canvas; home battery/time updates push small sprites. Waking from display-off renders the home frame before restoring the backlight, preventing the previous screensaver frame from flashing.

## Dashboard authorization

The default access state is `CoreDisplay::AccessState::idle`: boot generates no access code. On home, idle retains the network and dashboard URL, shows `Open dashboard to authorize`, and offers exactly `Authorize browser`. Granted (reauthorization), expired, and attempts-exhausted states use the same button label with their status message. Active codes have no authorization button; unavailable is non-actionable. `accessState()` still clears the previous secret, and `accessCode()` clears the previous buffer before copying a new code.

A code request is emitted only for a fresh `wasPressed()` touch inside the drawn button on an already visible home page. Saver/display-off contact only wakes; held contact does not authorize. Boot, fatal, OTA, and AI approval pages cannot emit code requests. Pure eligibility and shared drawing/hit-test bounds live in `core_display_policy.hpp`, with component regression tests covering states, wake/held contact, and target edges.

The application maps `BootstrapState::idle` to the idle home view and handles `Action::generate_access_code` through the physical-presence authorization path. Code creation and display updates stay on the core-0 application task; HTTP requests can check access or exchange an existing code, but cannot issue one.

Hardware tuning still required on a CoreS3:

- Confirm M5Unified's panel rotation, 320×240 dimensions, sprite allocation/PSRAM availability, rendering color order and actual backlight-off/wake behavior at brightness 0/160.
- Compare Font0/Font2 glyph widths and baseline alignment to embedded-graphics' 6×10 and 10×20 monospace fonts; M5GFX fonts are not pixel-identical. Tune text clipping/line lengths against real network strings.
- Check the AXP2101 `getBatteryLevel()` / `getVBUSVoltage()` readings and charging bolt against USB/battery states; test RTC validity after cold boot and subsequent time synchronization.
- Verify FT6336U touch sampling, wake latency, 5-minute saver / 15-minute display-off timings, 1-second saver clock redraw, and transfer/PSRAM throughput on hardware. No hardware behavior is asserted as tested here.
