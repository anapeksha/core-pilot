#include "models_codec.hpp"

#include <cstring>

namespace core_storage {
namespace {

constexpr std::uint8_t kMagic[] = {'M', 'D', 'B', '1'};

bool putByte(std::uint8_t value, std::uint8_t* output, std::size_t capacity,
             std::size_t& position) {
  if (position == capacity)
    return false;
  output[position++] = value;
  return true;
}

bool putString(const char* value, std::size_t limit, std::uint8_t* output, std::size_t capacity,
               std::size_t& position) {
  const std::size_t length = strnlen(value, limit + 1);
  if (length == 0 || length > limit || length > 255 || position + 1 + length > capacity)
    return false;
  output[position++] = static_cast<std::uint8_t>(length);
  std::memcpy(output + position, value, length);
  position += length;
  return true;
}

bool getByte(const std::uint8_t* input, std::size_t length, std::size_t& position,
             std::uint8_t& value) {
  if (position == length)
    return false;
  value = input[position++];
  return true;
}

bool getString(const std::uint8_t* input, std::size_t length, std::size_t limit,
               std::size_t& position, char* output) {
  std::uint8_t size = 0;
  if (!getByte(input, length, position, size) || size == 0 || size > limit ||
      position + size > length)
    return false;
  std::memcpy(output, input + position, size);
  output[size] = '\0';
  position += size;
  return true;
}

bool validModel(const core_domain::ModelUpsert& model, core_domain::ModelKind kind) {
  if (!model.has_id || strnlen(model.id, sizeof model.id) == 0 ||
      strnlen(model.id, sizeof model.id) > core_domain::max_id ||
      strnlen(model.name, sizeof model.name) == 0 ||
      strnlen(model.name, sizeof model.name) > core_domain::max_name)
    return false;
  const std::size_t detail_limit =
      kind == core_domain::ModelKind::group ? core_domain::max_members : core_domain::max_actions;
  if (kind == core_domain::ModelKind::room && model.detail_count != 0)
    return false;
  if (model.detail_count > detail_limit ||
      ((kind == core_domain::ModelKind::scene || kind == core_domain::ModelKind::automation) &&
       model.detail_count == 0))
    return false;
  if (kind == core_domain::ModelKind::automation &&
      (strnlen(model.trigger, sizeof model.trigger) == 0 ||
       strnlen(model.trigger, sizeof model.trigger) > core_domain::max_detail))
    return false;
  for (std::size_t index = 0; index < model.detail_count; ++index)
    if (strnlen(model.details[index], sizeof model.details[index]) == 0 ||
        strnlen(model.details[index], sizeof model.details[index]) > core_domain::max_detail)
      return false;
  return true;
}

} // namespace

core_security::Status encode_models(const ModelsDatabase& database, std::uint8_t* output,
                                    std::size_t capacity, std::size_t& written) {
  written = 0;
  if (!output || capacity < sizeof(kMagic))
    return output ? core_security::Status::BufferTooSmall : core_security::Status::Invalid;
  std::memcpy(output, kMagic, sizeof(kMagic));
  std::size_t position = sizeof(kMagic);
  for (std::size_t kind_index = 0; kind_index < kModelKinds; ++kind_index) {
    const auto& collection = database.collections[kind_index];
    if (collection.count > core_domain::max_models ||
        !putByte(static_cast<std::uint8_t>(collection.count), output, capacity, position))
      return collection.count > core_domain::max_models ? core_security::Status::Invalid
                                                        : core_security::Status::BufferTooSmall;
    const auto kind = static_cast<core_domain::ModelKind>(kind_index);
    for (std::size_t index = 0; index < collection.count; ++index) {
      const auto& model = collection.entries[index];
      if (!validModel(model, kind))
        return core_security::Status::Invalid;
      for (std::size_t prior = 0; prior < index; ++prior)
        if (std::strcmp(model.id, collection.entries[prior].id) == 0)
          return core_security::Status::Invalid;
      if (!putString(model.id, core_domain::max_id, output, capacity, position) ||
          !putString(model.name, core_domain::max_name, output, capacity, position))
        return core_security::Status::BufferTooSmall;
      if (kind == core_domain::ModelKind::automation) {
        if (!putByte(model.enabled ? 1 : 0, output, capacity, position) ||
            !putString(model.trigger, core_domain::max_detail, output, capacity, position))
          return core_security::Status::BufferTooSmall;
      }
      if (kind != core_domain::ModelKind::room) {
        if (!putByte(static_cast<std::uint8_t>(model.detail_count), output, capacity, position))
          return core_security::Status::BufferTooSmall;
        for (std::size_t detail = 0; detail < model.detail_count; ++detail)
          if (!putString(model.details[detail], core_domain::max_detail, output, capacity,
                         position))
            return core_security::Status::BufferTooSmall;
      }
    }
  }
  written = position;
  return core_security::Status::Ok;
}

core_security::Status decode_models(std::uint16_t schema, const std::uint8_t* input,
                                    std::size_t length, ModelsDatabase& database) {
  if (schema != kModelsSchema)
    return core_security::Status::UnsupportedFormat;
  if (!input || length < sizeof(kMagic) + kModelKinds || length > kMaxModelsPayload ||
      std::memcmp(input, kMagic, sizeof(kMagic)) != 0)
    return core_security::Status::Invalid;
  database = {};
  std::size_t position = sizeof(kMagic);
  for (std::size_t kind_index = 0; kind_index < kModelKinds; ++kind_index) {
    std::uint8_t count = 0;
    if (!getByte(input, length, position, count) || count > core_domain::max_models)
      return core_security::Status::Invalid;
    auto& collection = database.collections[kind_index];
    const auto kind = static_cast<core_domain::ModelKind>(kind_index);
    for (std::size_t index = 0; index < count; ++index) {
      auto& model = collection.entries[index];
      model.has_id = true;
      if (!getString(input, length, core_domain::max_id, position, model.id) ||
          !getString(input, length, core_domain::max_name, position, model.name))
        return core_security::Status::Invalid;
      if (kind == core_domain::ModelKind::automation) {
        std::uint8_t enabled = 0;
        if (!getByte(input, length, position, enabled) || enabled > 1 ||
            !getString(input, length, core_domain::max_detail, position, model.trigger))
          return core_security::Status::Invalid;
        model.enabled = enabled == 1;
      }
      if (kind != core_domain::ModelKind::room) {
        std::uint8_t details = 0;
        const std::size_t detail_limit = kind == core_domain::ModelKind::group
                                             ? core_domain::max_members
                                             : core_domain::max_actions;
        if (!getByte(input, length, position, details) || details > detail_limit)
          return core_security::Status::Invalid;
        model.detail_count = details;
        for (std::size_t detail = 0; detail < details; ++detail)
          if (!getString(input, length, core_domain::max_detail, position, model.details[detail]))
            return core_security::Status::Invalid;
      }
      if (!validModel(model, kind))
        return core_security::Status::Invalid;
      for (std::size_t prior = 0; prior < index; ++prior)
        if (std::strcmp(model.id, collection.entries[prior].id) == 0)
          return core_security::Status::Invalid;
    }
    collection.count = count;
  }
  if (position != length)
    return core_security::Status::Invalid;
  return core_security::Status::Ok;
}

} // namespace core_storage
