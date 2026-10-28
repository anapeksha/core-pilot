#include "core_display.hpp"

#include "core_board.hpp"
#include "esp_log.h"
#include "esp_timer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace core_pilot {
namespace {
constexpr char kTag[] = "core_display";

constexpr std::uint16_t rgb(int r, int g, int b) {
  return static_cast<std::uint16_t>((r << 11) | (g << 5) | b);
}
constexpr auto kBlack = rgb(0, 0, 0);
constexpr auto kWhite = rgb(31, 63, 31);
constexpr auto kBlue = rgb(4, 34, 31);
constexpr auto kDimBlue = rgb(1, 16, 24);
constexpr auto kMuted = rgb(12, 20, 24);
constexpr auto kCyan = rgb(3, 45, 30);
constexpr auto kClock = rgb(3, 50, 31);
constexpr auto kCharge = kWhite;
constexpr int kBatteryX = 286;
constexpr int kBatteryY = 5;
constexpr int kAccessButtonX = 16;
constexpr int kAccessButtonY = 176;
constexpr int kAccessButtonWidth = 180;
constexpr int kAccessButtonHeight = 44;
constexpr std::uint32_t kSaverMs = 300000;
constexpr std::uint32_t kOffMs = 900000;
std::uint32_t nowMs() {
  const auto milliseconds = static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U;
  return static_cast<std::uint32_t>(milliseconds);
}

template <std::size_t N> void copyLine(char (&dest)[N], const char* source) {
  std::snprintf(dest, N, "%s", source ? source : "");
}

void pushCanvas(M5Canvas& canvas, int x, int y) {
  const esp_err_t result = board::draw_lcd_region(x, y, canvas.width(), canvas.height(),
                                                  canvas.getBuffer(), canvas.bufferLength());
  if (result != ESP_OK)
    ESP_LOGE(kTag, "LCD transfer failed: %s", esp_err_to_name(result));
}

const char* phaseLabel(CoreDisplay::UpgradePhase phase, bool restart) {
  switch (phase) {
  case CoreDisplay::UpgradePhase::prepared:
    return "Preparing firmware upload";
  case CoreDisplay::UpgradePhase::staging:
    return "Receiving firmware upload";
  case CoreDisplay::UpgradePhase::applying:
    return "Installing verified firmware";
  case CoreDisplay::UpgradePhase::activated:
    return restart ? "Update complete; rebooting" : "Update complete; reboot from web";
  case CoreDisplay::UpgradePhase::rebooting:
    return "Rebooting into updated firmware";
  case CoreDisplay::UpgradePhase::failed:
    return "Update failed; previous firmware safe";
  }
  return "OTA idle";
}

constexpr int kDigitX[] = {22, 60, 122, 160, 222, 260};
constexpr std::uint8_t kSegments[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
constexpr int kRects[][4] = {{7, 1, 20, 6},  {28, 6, 6, 20}, {28, 33, 6, 20}, {7, 54, 20, 6},
                             {2, 33, 6, 20}, {2, 6, 6, 20},  {7, 28, 20, 6}};
constexpr const char* kMonths[] = {"January",   "February", "March",    "April",
                                   "May",       "June",     "July",     "August",
                                   "September", "October",  "November", "December"};
} // namespace

CoreDisplay::CoreDisplay()
    : canvas_(&M5.Display), time_sprite_(&M5.Display), battery_sprite_(&M5.Display) {}

esp_err_t CoreDisplay::begin() {
  if (initialized_)
    return ESP_ERR_INVALID_STATE;
  auto config = M5.config();
  config.clear_display = true;
  config.output_power = false;
  M5.begin(config);
  if (M5.Display.width() != 320 || M5.Display.height() != 240)
    return ESP_ERR_NOT_SUPPORTED;
  M5.Display.waitDMA();
  esp_err_t result = board::initialize_shared_spi();
  if (result != ESP_OK)
    return result;
  result = board::initialize_lcd();
  if (result != ESP_OK)
    return result;
  canvas_.setColorDepth(16);
  time_sprite_.setColorDepth(16);
  battery_sprite_.setColorDepth(16);
  if (!canvas_.createSprite(320, 240) || !time_sprite_.createSprite(50, 24) ||
      !battery_sprite_.createSprite(32, 14))
    return ESP_ERR_NO_MEM;
  initialized_ = true;
  last_touch_ms_ = nowMs();
  M5.Display.setBrightness(160);
  return ESP_OK;
}

void CoreDisplay::drawText(const char* text, int x, int baseline, std::uint16_t color, bool large) {
  canvas_.setTextDatum(top_left);
  canvas_.setTextColor(color, kBlack);
  if (large)
    canvas_.setFont(&fonts::Font2);
  else
    canvas_.setFont(&fonts::Font0);
  canvas_.drawString(text ? text : "", x, baseline - (large ? 20 : 10));
}

void CoreDisplay::push() {
  pushCanvas(canvas_, 0, 0);
}

void CoreDisplay::bootSplash() {
  if (!initialized_)
    return;
  page_ = Page::boot;
  saver_ = blanked_ = false;
  M5.Display.setBrightness(160);
  canvas_.fillScreen(kBlack);
  canvas_.drawRoundRect(132, 34, 56, 56, 12, rgb(9, 20, 22));
  canvas_.drawRoundRect(133, 35, 54, 54, 11, rgb(9, 20, 22));
  canvas_.drawWideLine(145, 62, 160, 49, 3, kWhite);
  canvas_.drawWideLine(160, 49, 175, 62, 3, kWhite);
  canvas_.drawWideLine(147, 60, 147, 77, 3, kWhite);
  canvas_.drawWideLine(173, 60, 173, 77, 3, kWhite);
  canvas_.drawWideLine(147, 77, 173, 77, 3, kWhite);
  canvas_.drawFastHLine(157, 67, 7, kCyan);
  canvas_.drawFastVLine(157, 67, 10, kCyan);
  canvas_.drawFastVLine(163, 67, 10, kCyan);
  drawText("CORE PILOT", 105, 160, kWhite, true);
  drawText("LOCAL HOME INTELLIGENCE", 91, 184, kCyan);
  canvas_.fillRect(76, 204, 168, 4, rgb(1, 22, 28));
  push();
}

void CoreDisplay::bootProgress(std::uint8_t percent, const char* label) {
  if (!initialized_ || page_ != Page::boot)
    return;
  canvas_.fillRect(0, 198, 320, 42, kBlack);
  canvas_.fillRect(76, 204, 168, 4, rgb(1, 22, 28));
  canvas_.fillRect(76, 204, std::min<int>(percent, 100) * 168 / 100, 4, kCyan);
  drawText(label, 76, 224, rgb(12, 24, 25));
  push();
}

void CoreDisplay::fatal(const FatalContent& content) {
  if (!initialized_)
    return;
  page_ = Page::fatal;
  saver_ = blanked_ = false;
  M5.Display.setBrightness(160);
  const auto red = rgb(29, 5, 5);
  const auto soft = rgb(10, 2, 2);
  canvas_.fillScreen(kBlack);
  canvas_.fillRect(0, 0, 320, 8, red);
  canvas_.fillRect(16, 24, 42, 42, soft);
  drawText("!", 31, 54, red, true);
  drawText(content.eyebrow, 72, 40, red);
  drawText(content.title, 16, 94, kWhite, true);
  drawText(content.detail, 16, 124, rgb(18, 22, 24));
  canvas_.fillRect(16, 154, 288, 52, soft);
  drawText("RECOVERY", 28, 174, red);
  drawText(content.action, 28, 194, kWhite);
  drawText("Core Pilot startup stopped", 16, 228, rgb(18, 22, 24));
  push();
}

bool CoreDisplay::rtcReady() {
  if (!initialized_)
    return false;
  m5::rtc_datetime_t value{};
  return M5.Rtc.getDateTime(&value) && value.time.hours >= 0 && value.time.hours <= 23 &&
         value.time.minutes >= 0 && value.time.minutes <= 59 && value.time.seconds >= 0 &&
         value.time.seconds <= 59 && value.date.month >= 1 && value.date.month <= 12 &&
         value.date.date >= 1 && value.date.date <= 31 && value.date.year >= 2000;
}

bool CoreDisplay::setRtcFromUnix(std::time_t timestamp) {
  if (!initialized_ || timestamp < 1704067200)
    return false;
  std::tm local{};
  if (localtime_r(&timestamp, &local) == nullptr || local.tm_year + 1900 < 2000 ||
      local.tm_year + 1900 > 2099)
    return false;
  m5::rtc_datetime_t value{};
  value.time.hours = static_cast<std::int8_t>(local.tm_hour);
  value.time.minutes = static_cast<std::int8_t>(local.tm_min);
  value.time.seconds = static_cast<std::int8_t>(local.tm_sec);
  value.date.year = static_cast<std::int16_t>(local.tm_year + 1900);
  value.date.month = static_cast<std::int8_t>(local.tm_mon + 1);
  value.date.date = static_cast<std::int8_t>(local.tm_mday);
  value.date.weekDay = static_cast<std::int8_t>(local.tm_wday);
  M5.Rtc.setDateTime(&value);
  return rtcReady();
}

bool CoreDisplay::readClock(int& hour, int& minute, int& second, int& day, int& month, int& year) {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  if (now >= 1704067200 && localtime_r(&now, &local) != nullptr) {
    hour = local.tm_hour;
    minute = local.tm_min;
    second = local.tm_sec;
    day = local.tm_mday;
    month = local.tm_mon + 1;
    year = local.tm_year + 1900;
    return true;
  }

  m5::rtc_datetime_t dt{};
  if (!M5.Rtc.getDateTime(&dt) || dt.time.hours < 0 || dt.time.hours > 23 || dt.time.minutes < 0 ||
      dt.time.minutes > 59 || dt.time.seconds < 0 || dt.time.seconds > 59 || dt.date.month < 1 ||
      dt.date.month > 12 || dt.date.date < 1 || dt.date.date > 31 || dt.date.year < 2000)
    return false;
  hour = static_cast<int>(static_cast<unsigned char>(dt.time.hours));
  minute = static_cast<int>(static_cast<unsigned char>(dt.time.minutes));
  second = static_cast<int>(static_cast<unsigned char>(dt.time.seconds));
  day = static_cast<int>(static_cast<unsigned char>(dt.date.date));
  month = static_cast<int>(static_cast<unsigned char>(dt.date.month));
  year = dt.date.year;
  return true;
}

void CoreDisplay::drawBattery(M5Canvas& target, int x, int y) {
  const auto color = battery_percent_ < 0 || battery_percent_ > 30 ? rgb(5, 54, 5)
                     : battery_percent_ > 15                       ? rgb(31, 48, 0)
                                                                   : rgb(31, 4, 2);
  target.fillRect(x, y, 32, 14, kBlack);
  target.drawRect(x + 1, y + 1, 27, 12, color);
  target.fillRect(x + 28, y + 5, 3, 5, color);
  const int fill = battery_percent_ < 0 ? 20 : std::min(battery_percent_, 100) * 20 / 100;
  if (fill)
    target.fillRect(x + 4, y + 4, fill, 6, color);
  if (external_power_) {
    target.fillTriangle(x + 18, y + 2, x + 13, y + 8, x + 17, y + 8, kCharge);
    target.fillTriangle(x + 16, y + 6, x + 20, y + 6, x + 14, y + 12, kCharge);
  }
}

void CoreDisplay::renderHome() {
  canvas_.fillScreen(kBlack);
  drawText("Core Pilot", 16, 30, kBlue, true);
  drawText("Local home intelligence", 16, 52, kDimBlue);
  drawText(network_, 16, 92, kBlue);
  drawText(web_, 16, 112, kBlue);
  if (access_state_ == AccessState::active) {
    drawText("Dashboard access code", 16, 158, kMuted);
    drawText(access_code_, 16, 194, kBlue);
  } else if (access_state_ == AccessState::granted || access_state_ == AccessState::expired ||
             access_state_ == AccessState::attempts_exhausted) {
    const char* message = access_state_ == AccessState::granted   ? "Dashboard access authorized"
                          : access_state_ == AccessState::expired ? "Access code expired"
                                                                  : "Access attempts exhausted";
    drawText(message, 16, 158, kMuted);
    canvas_.drawRoundRect(kAccessButtonX, kAccessButtonY, kAccessButtonWidth, kAccessButtonHeight,
                          8, kBlue);
    drawText(access_state_ == AccessState::granted ? "Authorize another browser"
                                                   : "Generate new access code",
             kAccessButtonX + 12, 204, kBlue);
  } else {
    drawText("Dashboard access unavailable", 16, 194, kMuted);
  }
  int h, m, s, d, mo, y;
  if (readClock(h, m, s, d, mo, y))
    std::snprintf(time_, sizeof(time_), "%02d:%02d", h, m);
  else
    copyLine(time_, "--:--");
  drawText(time_, 234, 24, kBlue, true);
  drawBattery(canvas_, kBatteryX, kBatteryY);
  push();
}

void CoreDisplay::clearAccessCode() {
  volatile char* value = access_code_;
  for (std::size_t i = 0; i < sizeof(access_code_); ++i)
    value[i] = 0;
}

void CoreDisplay::accessCode(const char* code) {
  if (!initialized_ || !code)
    return;
  clearAccessCode();
  std::snprintf(access_code_, sizeof(access_code_), "%s", code);
  access_state_ = AccessState::active;
  if (page_ == Page::home && !saver_ && !blanked_)
    renderHome();
}

void CoreDisplay::accessState(AccessState state) {
  if (!initialized_)
    return;
  clearAccessCode();
  access_state_ = state;
  if (page_ == Page::home && !saver_ && !blanked_)
    renderHome();
}

void CoreDisplay::home(const char* network, const char* web, const char* status) {
  if (!initialized_)
    return;
  copyLine(network_, network);
  copyLine(web_, web);
  copyLine(status_, status);
  page_ = Page::home;
  saver_ = blanked_ = false;
  last_touch_ms_ = nowMs();
  M5.Display.setBrightness(160);
  renderHome();
}

void CoreDisplay::renderUpgrade() {
  const auto total = upgrade_.total_bytes;
  const auto progress = std::min(upgrade_.progress_bytes, total);
  const auto percent =
      total ? static_cast<unsigned>(static_cast<std::uint64_t>(progress) * 100 / total) : 0U;
  char line[52];
  std::snprintf(line, sizeof(line), "%u%%  %lu / %lu KiB", percent,
                static_cast<unsigned long>(progress / 1024),
                static_cast<unsigned long>(total / 1024));
  canvas_.fillScreen(kBlack);
  drawText("Core Pilot", 16, 30, kBlue, true);
  drawText("Firmware upgrade", 16, 58, kMuted);
  drawText(phaseLabel(upgrade_.phase, upgrade_.restart_after_update), 16, 100, kBlue);
  canvas_.drawRect(16, 126, 256, 18, kMuted);
  const auto bar_width = static_cast<std::int32_t>(std::min(254U, percent * 256U / 100U));
  canvas_.fillRect(17, 127, bar_width, 16, kBlue);
  drawText(line, 16, 168, kBlue);
  drawText("Do not power off during update.", 16, 210, kMuted);
  if (upgrade_.phase == UpgradePhase::activated && !upgrade_.restart_after_update)
    drawText("Use System > Reboot when ready.", 16, 230, kMuted);
  push();
}

void CoreDisplay::upgrade(const Upgrade& progress) {
  if (!initialized_)
    return;
  upgrade_ = progress;
  page_ = Page::upgrade;
  saver_ = blanked_ = false;
  M5.Display.setBrightness(160);
  renderUpgrade();
}

void CoreDisplay::renderSaver() {
  canvas_.fillScreen(kBlack);
  for (auto& digit : digits_)
    digit = 255;
  date_[0] = '\0';
  for (int x : {96, 196}) {
    canvas_.fillRect(x + 5, 80, 12, 12, kClock);
    canvas_.fillRect(x + 5, 108, 12, 12, kClock);
  }
  push();
  updateClock();
}

void CoreDisplay::updateClock() {
  int h, m, s, d, mo, y;
  const bool valid = readClock(h, m, s, d, mo, y);
  if (saver_) {
    if (!valid) {
      const auto uptime = nowMs() / 1000 % 86400;
      h = static_cast<int>(uptime / 3600U);
      m = static_cast<int>((uptime / 60U) % 60U);
      s = static_cast<int>(uptime % 60U);
    }
    const std::uint8_t next[] = {
        static_cast<std::uint8_t>(h / 10), static_cast<std::uint8_t>(h % 10),
        static_cast<std::uint8_t>(m / 10), static_cast<std::uint8_t>(m % 10),
        static_cast<std::uint8_t>(s / 10), static_cast<std::uint8_t>(s % 10)};
    bool changed = false;
    for (int i = 0; i < 6; ++i) {
      if (digits_[i] == next[i])
        continue;
      changed = true;
      const int x = kDigitX[i];
      canvas_.fillRect(x, 68, 34, 64, kBlack);
      for (int bit = 0; bit < 7; ++bit) {
        if (kSegments[next[i]] & (1 << bit)) {
          const auto& r = kRects[bit];
          canvas_.fillRect(x + r[0], 68 + r[1], r[2], r[3], kClock);
        }
      }
      digits_[i] = next[i];
    }
    char next_date[sizeof(date_)];
    if (valid)
      std::snprintf(next_date, sizeof(next_date), "%d %s %d", d, kMonths[mo - 1], y);
    else
      copyLine(next_date, "syncing time");
    if (std::strcmp(date_, next_date) != 0) {
      copyLine(date_, next_date);
      canvas_.fillRect(0, 148, 320, 24, kBlack);
      canvas_.setFont(&fonts::Font2);
      const int date_width = static_cast<int>(canvas_.textWidth(date_));
      const int date_x = std::max(0, (320 - date_width) / 2);
      drawText(date_, date_x, 167, rgb(2, 28, 24), true);
      changed = true;
    }
    if (changed)
      push();
  } else if (page_ == Page::home) {
    char next[sizeof(time_)];
    if (valid)
      std::snprintf(next, sizeof(next), "%02d:%02d", h, m);
    else
      copyLine(next, "--:--");
    if (std::strcmp(time_, next) != 0) {
      copyLine(time_, next);
      time_sprite_.fillScreen(kBlack);
      time_sprite_.setTextDatum(top_left);
      time_sprite_.setFont(&fonts::Font2);
      time_sprite_.setTextColor(kBlue, kBlack);
      time_sprite_.drawString(time_, 0, 0);
      pushCanvas(time_sprite_, 234, 4);
    }
  }
}

void CoreDisplay::updateBattery() {
  const int level = M5.Power.getBatteryLevel();
  const int next = level >= 0 && level <= 100 ? level : -1;
  const int vbus_mv = M5.Power.getVBUSVoltage();
  const float base_input_volts = M5.Power.Axp2101.getTSVoltage();
  const bool charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
  const bool base_power = !M5.Power.getExtOutput() && base_input_volts >= 2.0f;
  const bool plugged = charging || vbus_mv >= 4000 || base_power;
  if (next == battery_percent_ && plugged == external_power_)
    return;
  battery_percent_ = next;
  external_power_ = plugged;
  if (page_ == Page::home && !saver_ && !blanked_) {
    drawBattery(battery_sprite_, 0, 0);
    pushCanvas(battery_sprite_, kBatteryX, kBatteryY);
    drawBattery(canvas_, kBatteryX, kBatteryY);
  }
}

CoreDisplay::Action CoreDisplay::poll() {
  if (!initialized_)
    return Action::none;
  M5.update();
  const auto now = nowMs();
  Action action = Action::none;
  if (M5.Touch.getCount() > 0) {
    last_touch_ms_ = now;
    const bool waking = page_ == Page::home && (saver_ || blanked_);
    if (waking) {
      const bool was_blanked = blanked_;
      saver_ = blanked_ = false;
      renderHome();
      if (was_blanked)
        M5.Display.setBrightness(160);
    } else if (page_ == Page::home &&
               (access_state_ == AccessState::granted || access_state_ == AccessState::expired ||
                access_state_ == AccessState::attempts_exhausted)) {
      const auto& touch = M5.Touch.getDetail();
      if (touch.wasPressed() && touch.x >= kAccessButtonX &&
          touch.x < kAccessButtonX + kAccessButtonWidth && touch.y >= kAccessButtonY &&
          touch.y < kAccessButtonY + kAccessButtonHeight)
        action = Action::generate_access_code;
    }
  }
  if (static_cast<std::uint32_t>(now - last_battery_ms_) >= 100 || last_battery_ms_ == 0) {
    last_battery_ms_ = now;
    updateBattery();
  }
  if (page_ != Page::home)
    return action;
  const auto idle = static_cast<std::uint32_t>(now - last_touch_ms_);
  if (!blanked_ && idle >= kOffMs) {
    M5.Display.setBrightness(0);
    blanked_ = true;
    saver_ = false;
  }
  if (blanked_)
    return action;
  if (!saver_ && idle >= kSaverMs) {
    saver_ = true;
    renderSaver();
  }
  if (static_cast<std::uint32_t>(now - last_clock_ms_) >= (saver_ ? 1000U : 5000U)) {
    last_clock_ms_ = now;
    updateClock();
  }
  return action;
}

} // namespace core_pilot
