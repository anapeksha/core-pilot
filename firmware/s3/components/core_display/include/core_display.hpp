#pragma once

#include "core_display_policy.hpp"
#include "esp_err.h"

#include <M5Unified.h>
#include <cstdint>
#include <ctime>

namespace core_pilot {

/**
 * Owns CoreS3 display, touch, power, and RTC access.
 *
 * All methods must be called from the same task. Other components must not use
 * M5.begin(), M5.update(), M5.Display, M5.Touch, M5.Power, or M5.Rtc while this
 * component is active.
 */
class CoreDisplay final {
public:
  struct FatalContent {
    const char* eyebrow;
    const char* title;
    const char* detail;
    const char* action;
  };
  using UpgradePhase = display_policy::UpgradePhase;
  enum class AccessState : std::uint8_t {
    active,
    granted,
    expired,
    attempts_exhausted,
    unavailable
  };
  enum class Action : std::uint8_t {
    none,
    generate_access_code,
    approve_ai_action,
    reject_ai_action
  };
  using Upgrade = display_policy::Upgrade;

  CoreDisplay();
  CoreDisplay(const CoreDisplay&) = delete;
  CoreDisplay& operator=(const CoreDisplay&) = delete;

  /** Initializes M5Unified and display resources. */
  esp_err_t begin();

  /** Processes touch, power, clock, screensaver, and wake events. */
  Action poll();

  /** Returns whether the hardware RTC contains a validated calendar value. */
  bool rtcReady();

  /** Writes local calendar time derived from a synchronized Unix timestamp to the RTC. */
  bool setRtcFromUnix(std::time_t timestamp);

  /** Shows the initial boot screen. */
  void bootSplash();

  /** Updates boot progress and its human-readable label. */
  void bootProgress(std::uint8_t percent, const char* label);

  /** Shows a persistent fatal-error page. */
  void fatal(const FatalContent& content);

  /** Shows the primary status page and resets the idle timer. */
  void home(const char* network, const char* web, const char* status);

  /** Updates the physical-only dashboard access state. */
  void accessCode(const char* code);
  void accessState(AccessState state);

  /** Shows a locally generated AI action proposal for physical approval. */
  void approval(const char* summary);

  /** Returns from an AI approval page to the primary status page. */
  void clearApproval();

  /** Shows firmware-upgrade progress and prevents idle sleep. */
  void upgrade(const Upgrade& progress);

private:
  enum class Page : std::uint8_t { boot, fatal, home, approval, upgrade };
  void renderHome();
  void clearAccessCode();
  void renderApproval();
  void renderSaver();
  void renderUpgrade();
  void updateClock();
  void updateBattery();
  void drawBattery(M5Canvas& target, int x, int y);
  bool readClock(int& hour, int& minute, int& second, int& day, int& month, int& year);
  void drawText(const char* text, int x, int baseline, std::uint16_t color, bool large = false);
  void push();

  M5Canvas canvas_;
  M5Canvas time_sprite_;
  M5Canvas battery_sprite_;
  Page page_{Page::boot};
  Upgrade upgrade_{};
  char network_[51]{};
  char web_[51]{};
  char status_[51]{};
  char access_code_[17]{};
  char approval_summary_[97]{};
  AccessState access_state_{AccessState::unavailable};
  char time_[6]{};
  char date_[32]{};
  std::uint8_t digits_[6]{255, 255, 255, 255, 255, 255};
  int battery_percent_{-1};
  bool external_power_{false};
  bool initialized_{false};
  bool saver_{false};
  bool blanked_{false};
  std::uint32_t last_touch_ms_{0};
  std::uint32_t last_clock_ms_{0};
  std::uint32_t last_battery_ms_{0};
};

} // namespace core_pilot
