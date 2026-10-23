#include "host/ble_gap.h"

#include <cstdint>

extern "C" int __real_ble_gap_disc( // NOLINT(bugprone-reserved-identifier)
    std::uint8_t own_addr_type, std::int32_t duration_ms, const ble_gap_disc_params* disc_params,
    ble_gap_event_fn* callback, void* callback_arg);

extern "C" int __wrap_ble_gap_disc( // NOLINT(bugprone-reserved-identifier)
    std::uint8_t own_addr_type, std::int32_t duration_ms, const ble_gap_disc_params* disc_params,
    ble_gap_event_fn* callback, void* callback_arg) {
  if (disc_params == nullptr)
    return __real_ble_gap_disc(own_addr_type, duration_ms, nullptr, callback, callback_arg);

  ble_gap_disc_params active_params = *disc_params;
  active_params.passive = 0;
  return __real_ble_gap_disc(own_addr_type, duration_ms, &active_params, callback, callback_arg);
}
