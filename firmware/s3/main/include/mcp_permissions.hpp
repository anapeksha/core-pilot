#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace core_pilot::mcp_permissions {

inline constexpr std::string_view v1_path{"/core-pilot/ai/tool-permissions.enc"};
inline constexpr std::string_view v2_path{"/core-pilot/ai/tool-permissions-v2.enc"};
inline constexpr std::uint16_t v2_schema = 2;
inline constexpr std::string_view v1_tool_ids[] = {
    "devices.list",        "devices.get_state",    "devices.set_power",
    "devices.set_level",   "devices.set_color",    "devices.set_color_temperature",
    "scenes.activate",     "scenes.list",          "automations.list",
    "automations.enable",  "automations.disable",  "automations.create_simple",
    "hub.status",          "matter.get_operation", "matter.remove_device",
    "matter.reset_fabric", "hub.restart",          "hub.shutdown",
    "hub.factory_reset",
};

constexpr bool v1_enabled(std::uint32_t old_mask, std::string_view tool_id) {
  for (std::size_t index = 0; index < std::size(v1_tool_ids); ++index)
    if (v1_tool_ids[index] == tool_id)
      return (old_mask & (1U << index)) != 0;
  return false;
}

static_assert(v1_path != v2_path);
static_assert(std::size(v1_tool_ids) == 19);
static_assert(v1_enabled(1U << 12U, "hub.status"));
static_assert(!v1_enabled(1U << 12U, "automations.update"));

} // namespace core_pilot::mcp_permissions
