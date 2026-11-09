#pragma once

#include <cstdint>

namespace core_pilot {

bool enqueueMatterLocalForget(std::uint64_t node_id);
bool takeMatterLocalForget(std::uint64_t& node_id);

} // namespace core_pilot
