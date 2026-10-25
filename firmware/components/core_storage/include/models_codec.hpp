#pragma once

#include "core_domain.hpp"
#include "core_security.hpp"

#include <cstddef>
#include <cstdint>

namespace core_storage {

inline constexpr std::uint16_t kModelsSchema = 1;
inline constexpr std::size_t kModelKinds = 4;
inline constexpr std::size_t kMaxModelsPayload =
    8 + kModelKinds * core_domain::max_models *
            (4 + core_domain::max_id + core_domain::max_name + core_domain::max_detail +
             core_domain::max_members * (1 + core_domain::max_detail));

struct ModelCollection final {
  core_domain::ModelUpsert entries[core_domain::max_models]{};
  std::size_t count{0};
};

struct ModelsDatabase final {
  ModelCollection collections[kModelKinds]{};
};

constexpr std::size_t model_kind_index(core_domain::ModelKind kind) {
  return static_cast<std::size_t>(kind);
}

core_security::Status encode_models(const ModelsDatabase& database, std::uint8_t* output,
                                    std::size_t capacity, std::size_t& written);
core_security::Status decode_models(std::uint16_t schema, const std::uint8_t* input,
                                    std::size_t length, ModelsDatabase& database);

} // namespace core_storage
