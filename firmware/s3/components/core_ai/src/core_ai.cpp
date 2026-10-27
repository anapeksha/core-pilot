#include "core_ai.hpp"

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/platform_util.h"

#include <cctype>
#include <cmath>
#include <cstring>

namespace core_pilot::ai {
namespace {

constexpr char kProviderPath[] = "/core-pilot/config/ai_provider.enc";
constexpr char kMemoryPath[] = "/core-pilot/ai/memory-v2.enc";
constexpr char kPromptsPath[] = "/core-pilot/ai/prompts.enc";
constexpr std::uint16_t kSchema = 1;
constexpr std::size_t kRecordOverhead = 64;
constexpr std::size_t kProviderPayload =
    16 + kMaxProviderUrlBytes + kMaxProviderModelBytes + kMaxApiKeyBytes;
constexpr std::size_t kMemoryPayload =
    8 + kMaxMemorySections * (2 + kMaxMemoryIdBytes + 4 + kMaxMemoryMarkdownBytes);
constexpr std::size_t kPromptPayload =
    8 + kMaxPromptFiles * (2 + kMaxPromptPathBytes + 4 + kMaxPromptMarkdownBytes + 1);
constexpr std::uint8_t kProviderMagic[] = {'A', 'I', 'P', '1'};
constexpr std::uint8_t kMemoryMagic[] = {'A', 'I', 'M', '1'};
constexpr std::uint8_t kPromptMagic[] = {'A', 'I', 'F', '1'};

struct Workspace final {
  explicit Workspace(std::size_t plaintext_capacity)
      : plaintext_capacity(plaintext_capacity),
        encrypted_capacity(plaintext_capacity + kRecordOverhead) {
    plaintext = static_cast<std::uint8_t*>(
        heap_caps_malloc(plaintext_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    encrypted = static_cast<std::uint8_t*>(
        heap_caps_malloc(encrypted_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  ~Workspace() {
    if (plaintext) {
      mbedtls_platform_zeroize(plaintext, plaintext_capacity);
      heap_caps_free(plaintext);
    }
    if (encrypted) {
      mbedtls_platform_zeroize(encrypted, encrypted_capacity);
      heap_caps_free(encrypted);
    }
  }
  bool valid() const {
    return plaintext && encrypted;
  }
  std::uint8_t* plaintext{};
  std::uint8_t* encrypted{};
  std::size_t plaintext_capacity{};
  std::size_t encrypted_capacity{};
};

struct Lock final {
  explicit Lock(SemaphoreHandle_t mutex)
      : mutex(mutex), held(mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {}
  ~Lock() {
    if (held)
      xSemaphoreGive(mutex);
  }
  SemaphoreHandle_t mutex{};
  bool held{};
};

Status map_status(core_security::Status status) {
  switch (status) {
  case core_security::Status::Ok:
    return Status::Ok;
  case core_security::Status::NotFound:
    return Status::NotFound;
  case core_security::Status::Invalid:
  case core_security::Status::AuthenticationFailed:
    return Status::CorruptData;
  case core_security::Status::TooLarge:
  case core_security::Status::BufferTooSmall:
    return Status::TooLarge;
  case core_security::Status::UnsupportedFormat:
    return Status::UnsupportedFormat;
  case core_security::Status::IoError:
  case core_security::Status::CryptoError:
    return Status::StorageError;
  }
  return Status::StorageError;
}

bool put8(std::uint8_t value, std::uint8_t* out, std::size_t cap, std::size_t& pos) {
  if (pos >= cap)
    return false;
  out[pos++] = value;
  return true;
}
bool put16(std::uint16_t value, std::uint8_t* out, std::size_t cap, std::size_t& pos) {
  return put8(static_cast<std::uint8_t>(value), out, cap, pos) &&
         put8(static_cast<std::uint8_t>(value >> 8), out, cap, pos);
}
bool put32(std::uint32_t value, std::uint8_t* out, std::size_t cap, std::size_t& pos) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    if (!put8(static_cast<std::uint8_t>(value >> shift), out, cap, pos))
      return false;
  return true;
}
bool putBytes(const void* data, std::size_t size, std::uint8_t* out, std::size_t cap,
              std::size_t& pos) {
  if ((!data && size) || size > cap - pos)
    return false;
  if (size)
    std::memcpy(out + pos, data, size);
  pos += size;
  return true;
}
bool get8(const std::uint8_t* in, std::size_t len, std::size_t& pos, std::uint8_t& value) {
  if (pos >= len)
    return false;
  value = in[pos++];
  return true;
}
bool get16(const std::uint8_t* in, std::size_t len, std::size_t& pos, std::uint16_t& value) {
  std::uint8_t lo{}, hi{};
  if (!get8(in, len, pos, lo) || !get8(in, len, pos, hi))
    return false;
  value = static_cast<std::uint16_t>(lo | static_cast<std::uint16_t>(hi << 8));
  return true;
}
bool get32(const std::uint8_t* in, std::size_t len, std::size_t& pos, std::uint32_t& value) {
  value = 0;
  for (unsigned shift = 0; shift < 32; shift += 8) {
    std::uint8_t byte{};
    if (!get8(in, len, pos, byte))
      return false;
    value |= static_cast<std::uint32_t>(byte) << shift;
  }
  return true;
}
bool take(const std::uint8_t* in, std::size_t len, std::size_t& pos, std::size_t size,
          const std::uint8_t*& value) {
  if (size > len - pos)
    return false;
  value = in + pos;
  pos += size;
  return true;
}

bool valid_text(const char* value, std::size_t maximum, bool required = true) {
  if (!value)
    return false;
  const std::size_t size = strnlen(value, maximum + 1);
  return size <= maximum && (!required || size != 0);
}
bool valid_id(const char* value) {
  if (!valid_text(value, kMaxMemoryIdBytes))
    return false;
  for (const char* p = value; *p; ++p)
    if (!(std::isalnum(static_cast<unsigned char>(*p)) || *p == '_' || *p == '-'))
      return false;
  return true;
}
bool valid_path(const char* value) {
  if (!valid_text(value, kMaxPromptPathBytes))
    return false;
  for (const char* p = value; *p; ++p)
    if (!(std::isalnum(static_cast<unsigned char>(*p)) || *p == '_' || *p == '-' || *p == '.' ||
          *p == '/'))
      return false;
  return true;
}
// Only the destination authority and authentication mode bind a retained credential;
// model and URL path changes do not transfer it to a different origin.
bool credential_scope(const ProviderConfig& config, char* host, std::size_t capacity) {
  const char* url = config.base_url;
  if (std::strncmp(url, "https://", 8) != 0)
    return false;
  const char* begin = url + 8;
  const char* end = std::strpbrk(begin, "/?#");
  if (!end)
    end = begin + std::strlen(begin);
  const char* colon = static_cast<const char*>(std::memchr(begin, ':', end - begin));
  const char* host_end = colon ? colon : end;
  const std::size_t length = static_cast<std::size_t>(host_end - begin);
  if (length == 0 || length >= capacity ||
      (colon && (end - colon != 4 || std::memcmp(colon, ":443", 4) != 0)))
    return false;
  for (std::size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(begin[i]);
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
          c == '-'))
      return false;
    host[i] = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  }
  host[length] = '\0';
  return true;
}

bool same_credential_scope(const ProviderConfig& incoming, const ProviderConfig& stored) {
  char incoming_host[kMaxProviderUrlBytes + 1]{};
  char stored_host[kMaxProviderUrlBytes + 1]{};
  return incoming.backend == stored.backend && incoming.auth_type == stored.auth_type &&
         credential_scope(incoming, incoming_host, sizeof incoming_host) &&
         credential_scope(stored, stored_host, sizeof stored_host) &&
         std::strcmp(incoming_host, stored_host) == 0;
}

bool valid_provider(const ProviderConfig& config) {
  return static_cast<unsigned>(config.backend) <=
             static_cast<unsigned>(ProviderBackend::AnthropicCompatible) &&
         static_cast<unsigned>(config.auth_type) <= static_cast<unsigned>(AuthType::None) &&
         valid_text(config.base_url, kMaxProviderUrlBytes) &&
         valid_text(config.model, kMaxProviderModelBytes) && config.max_output_tokens >= 1 &&
         config.max_output_tokens <= 8192 && config.timeout_ms >= 1000 &&
         config.timeout_ms <= 120000 &&
         (!config.has_temperature || std::isfinite(config.temperature)) &&
         config.api_key_length <= kMaxApiKeyBytes;
}

bool supported_provider(const ProviderConfig& config) {
  char host[kMaxProviderUrlBytes + 1]{};
  if (!valid_provider(config) || config.api_key_length == 0 ||
      !credential_scope(config, host, sizeof host) ||
      std::strpbrk(config.base_url, "?#") != nullptr ||
      std::strchr(config.base_url + 8, '@') != nullptr)
    return false;
  if (config.backend == ProviderBackend::OpenAiCompatible)
    return config.auth_type == AuthType::Bearer && std::strcmp(host, "api.openai.com") == 0;
  return config.backend == ProviderBackend::AnthropicCompatible &&
         config.auth_type == AuthType::ApiKey && std::strcmp(host, "api.anthropic.com") == 0;
}

core_security::RecordSpec spec(const char* path, std::size_t path_len,
                               core_security::RecordType type, std::uint64_t generation) {
  return {path, path_len, core_security::Namespace::Ai, type, kSchema, generation};
}

Status load_record(core_security::StorageService& storage, const char* path, std::size_t path_len,
                   core_security::RecordType type, Workspace& workspace, std::size_t& size,
                   std::uint64_t& generation) {
  core_security::Metadata metadata{};
  const auto result =
      storage.get(spec(path, path_len, type, 0), workspace.encrypted, workspace.encrypted_capacity,
                  workspace.plaintext, workspace.plaintext_capacity, size, metadata);
  if (result == core_security::Status::Ok) {
    if (metadata.schema_version != kSchema)
      return Status::UnsupportedFormat;
    generation = metadata.generation;
  }
  return map_status(result);
}

Status save_record(core_security::StorageService& storage, core_security::RandomSource& random,
                   const char* path, std::size_t path_len, core_security::RecordType type,
                   Workspace& workspace, std::size_t size, std::uint64_t generation) {
  return map_status(storage.put(spec(path, path_len, type, generation + 1), workspace.plaintext,
                                size, random, workspace.encrypted, workspace.encrypted_capacity));
}

struct CommonState {
  StaticSemaphore_t mutex_buffer{};
  SemaphoreHandle_t mutex{};
};
Status start_state(CommonState*& state) {
  if (state)
    return Status::Ok;
  auto* candidate = static_cast<CommonState*>(
      heap_caps_calloc(1, sizeof(CommonState), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!candidate)
    return Status::StorageError;
  candidate->mutex = xSemaphoreCreateMutexStatic(&candidate->mutex_buffer);
  if (!candidate->mutex) {
    heap_caps_free(candidate);
    return Status::StorageError;
  }
  state = candidate;
  return Status::Ok;
}
void destroy_state(CommonState*& state) {
  if (!state)
    return;
  if (state->mutex)
    vSemaphoreDelete(state->mutex);
  mbedtls_platform_zeroize(state, sizeof(*state));
  heap_caps_free(state);
  state = nullptr;
}

} // namespace

struct ProviderConfigRepository::State : CommonState {};
struct MemoryRepository::State : CommonState {};
struct PromptRepository::State : CommonState {};

void clear_provider_config(ProviderConfig& config) {
  mbedtls_platform_zeroize(&config, sizeof(config));
}

bool provider_config_usable(const ProviderConfig& config) {
  return supported_provider(config);
}

ProviderConfigRepository::ProviderConfigRepository(core_security::StorageService& storage,
                                                   core_security::RandomSource& random)
    : storage_(storage), random_(random) {}
ProviderConfigRepository::~ProviderConfigRepository() {
  CommonState* common = state_;
  destroy_state(common);
  state_ = nullptr;
}
Status ProviderConfigRepository::start() {
  CommonState* common = state_;
  const Status result = start_state(common);
  state_ = static_cast<State*>(common);
  return result;
}
Status ProviderConfigRepository::load(ProviderConfig& output) {
  clear_provider_config(output);
  if (!state_)
    return Status::NotReady;
  Lock lock(state_->mutex);
  if (!lock.held)
    return Status::StorageError;
  Workspace workspace(kProviderPayload);
  if (!workspace.valid())
    return Status::StorageError;
  std::size_t size{};
  std::uint64_t generation{};
  Status result = load_record(storage_, kProviderPath, sizeof(kProviderPath) - 1,
                              core_security::RecordType::AiProvider, workspace, size, generation);
  if (result != Status::Ok)
    return result;
  std::size_t pos{};
  if (size < sizeof(kProviderMagic) ||
      std::memcmp(workspace.plaintext, kProviderMagic, sizeof(kProviderMagic)) != 0)
    return Status::CorruptData;
  pos = sizeof(kProviderMagic);
  std::uint8_t backend{}, auth{}, flags{};
  std::uint16_t url_size{}, model_size{}, key_size{};
  std::uint32_t temperature_bits{};
  const std::uint8_t* bytes{};
  if (!get8(workspace.plaintext, size, pos, backend) ||
      !get8(workspace.plaintext, size, pos, auth) || !get8(workspace.plaintext, size, pos, flags) ||
      !get32(workspace.plaintext, size, pos, output.max_output_tokens) ||
      !get32(workspace.plaintext, size, pos, output.timeout_ms) ||
      !get32(workspace.plaintext, size, pos, temperature_bits) ||
      !get16(workspace.plaintext, size, pos, url_size) || url_size > kMaxProviderUrlBytes ||
      !take(workspace.plaintext, size, pos, url_size, bytes))
    return Status::CorruptData;
  std::memcpy(output.base_url, bytes, url_size);
  if (!get16(workspace.plaintext, size, pos, model_size) || model_size > kMaxProviderModelBytes ||
      !take(workspace.plaintext, size, pos, model_size, bytes))
    return Status::CorruptData;
  std::memcpy(output.model, bytes, model_size);
  if (!get16(workspace.plaintext, size, pos, key_size) || key_size > kMaxApiKeyBytes ||
      !take(workspace.plaintext, size, pos, key_size, bytes) || pos != size)
    return Status::CorruptData;
  std::memcpy(output.api_key, bytes, key_size);
  output.backend = static_cast<ProviderBackend>(backend);
  output.auth_type = static_cast<AuthType>(auth);
  output.has_temperature = (flags & 1U) != 0;
  output.api_key_redacted = key_size != 0;
  output.api_key_length = key_size;
  std::memcpy(&output.temperature, &temperature_bits, sizeof(output.temperature));
  if (!provider_config_usable(output)) {
    clear_provider_config(output);
    return Status::CorruptData;
  }
  return Status::Ok;
}
Status ProviderConfigRepository::save(ProviderConfig& config) {
  struct Wipe final {
    ProviderConfig& value;
    ~Wipe() {
      mbedtls_platform_zeroize(value.api_key, sizeof(value.api_key));
      value.api_key_length = 0;
    }
  } wipe{config};
  if (!state_)
    return Status::NotReady;
  if (!valid_provider(config))
    return Status::InvalidArgument;
  Lock lock(state_->mutex);
  if (!lock.held)
    return Status::StorageError;
  ProviderConfig old{};
  struct ClearOld final {
    ProviderConfig& value;
    ~ClearOld() {
      clear_provider_config(value);
    }
  } clear_old{old};
  if (config.api_key_redacted && config.api_key_length == 0) {
    Workspace old_workspace(kProviderPayload);
    if (!old_workspace.valid())
      return Status::StorageError;
    std::size_t old_size{};
    std::uint64_t old_generation{};
    const Status loaded =
        load_record(storage_, kProviderPath, sizeof(kProviderPath) - 1,
                    core_security::RecordType::AiProvider, old_workspace, old_size, old_generation);
    if (loaded != Status::Ok)
      return loaded;
    // Decode the prior scope and key without taking the repository lock a second time.
    std::size_t pos = sizeof(kProviderMagic);
    std::uint8_t backend{}, auth{};
    std::uint16_t part{};
    const std::uint8_t* bytes{};
    if (old_size < pos ||
        std::memcmp(old_workspace.plaintext, kProviderMagic, sizeof(kProviderMagic)) != 0 ||
        !get8(old_workspace.plaintext, old_size, pos, backend) ||
        !get8(old_workspace.plaintext, old_size, pos, auth) || pos + 1 + 4 + 4 + 4 > old_size)
      return Status::CorruptData;
    old.backend = static_cast<ProviderBackend>(backend);
    old.auth_type = static_cast<AuthType>(auth);
    pos += 1 + 4 + 4 + 4;
    if (!get16(old_workspace.plaintext, old_size, pos, part) || part > kMaxProviderUrlBytes ||
        !take(old_workspace.plaintext, old_size, pos, part, bytes) ||
        std::memchr(bytes, '\0', part) != nullptr)
      return Status::CorruptData;
    std::memcpy(old.base_url, bytes, part);
    if (!get16(old_workspace.plaintext, old_size, pos, part) || part > kMaxProviderModelBytes ||
        !take(old_workspace.plaintext, old_size, pos, part, bytes) ||
        !get16(old_workspace.plaintext, old_size, pos, part) || part > kMaxApiKeyBytes ||
        !take(old_workspace.plaintext, old_size, pos, part, bytes) || pos != old_size)
      return Status::CorruptData;
    if (!same_credential_scope(config, old))
      return Status::InvalidArgument;
    config.api_key_length = part;
    std::memcpy(config.api_key, bytes, part);
  }
  if (!provider_config_usable(config))
    return Status::InvalidArgument;
  Workspace workspace(kProviderPayload);
  if (!workspace.valid())
    return Status::StorageError;
  std::size_t pos{};
  std::uint32_t temperature_bits{};
  std::memcpy(&temperature_bits, &config.temperature, sizeof(temperature_bits));
  const std::size_t url_size = std::strlen(config.base_url);
  const std::size_t model_size = std::strlen(config.model);
  if (!putBytes(kProviderMagic, sizeof(kProviderMagic), workspace.plaintext,
                workspace.plaintext_capacity, pos) ||
      !put8(static_cast<std::uint8_t>(config.backend), workspace.plaintext,
            workspace.plaintext_capacity, pos) ||
      !put8(static_cast<std::uint8_t>(config.auth_type), workspace.plaintext,
            workspace.plaintext_capacity, pos) ||
      !put8(config.has_temperature ? 1 : 0, workspace.plaintext, workspace.plaintext_capacity,
            pos) ||
      !put32(config.max_output_tokens, workspace.plaintext, workspace.plaintext_capacity, pos) ||
      !put32(config.timeout_ms, workspace.plaintext, workspace.plaintext_capacity, pos) ||
      !put32(temperature_bits, workspace.plaintext, workspace.plaintext_capacity, pos) ||
      !put16(static_cast<std::uint16_t>(url_size), workspace.plaintext,
             workspace.plaintext_capacity, pos) ||
      !putBytes(config.base_url, url_size, workspace.plaintext, workspace.plaintext_capacity,
                pos) ||
      !put16(static_cast<std::uint16_t>(model_size), workspace.plaintext,
             workspace.plaintext_capacity, pos) ||
      !putBytes(config.model, model_size, workspace.plaintext, workspace.plaintext_capacity, pos) ||
      !put16(config.api_key_length, workspace.plaintext, workspace.plaintext_capacity, pos) ||
      !putBytes(config.api_key, config.api_key_length, workspace.plaintext,
                workspace.plaintext_capacity, pos))
    return Status::TooLarge;
  std::size_t ignored{};
  std::uint64_t generation{};
  const Status loaded =
      load_record(storage_, kProviderPath, sizeof(kProviderPath) - 1,
                  core_security::RecordType::AiProvider, workspace, ignored, generation);
  if (loaded != Status::Ok && loaded != Status::NotFound)
    return loaded;
  // load_record overwrote plaintext when present, so encode again recursively is avoided by using
  // metadata generation 0 for replacement. Existing generation is recovered above; re-encode now.
  pos = 0;
  if (!putBytes(kProviderMagic, sizeof(kProviderMagic), workspace.plaintext,
                workspace.plaintext_capacity, pos) ||
      !put8(static_cast<std::uint8_t>(config.backend), workspace.plaintext,
            workspace.plaintext_capacity, pos) ||
      !put8(static_cast<std::uint8_t>(config.auth_type), workspace.plaintext,
            workspace.plaintext_capacity, pos) ||
      !put8(config.has_temperature ? 1 : 0, workspace.plaintext, workspace.plaintext_capacity,
            pos) ||
      !put32(config.max_output_tokens, workspace.plaintext, workspace.plaintext_capacity, pos) ||
      !put32(config.timeout_ms, workspace.plaintext, workspace.plaintext_capacity, pos) ||
      !put32(temperature_bits, workspace.plaintext, workspace.plaintext_capacity, pos) ||
      !put16(static_cast<std::uint16_t>(url_size), workspace.plaintext,
             workspace.plaintext_capacity, pos) ||
      !putBytes(config.base_url, url_size, workspace.plaintext, workspace.plaintext_capacity,
                pos) ||
      !put16(static_cast<std::uint16_t>(model_size), workspace.plaintext,
             workspace.plaintext_capacity, pos) ||
      !putBytes(config.model, model_size, workspace.plaintext, workspace.plaintext_capacity, pos) ||
      !put16(config.api_key_length, workspace.plaintext, workspace.plaintext_capacity, pos) ||
      !putBytes(config.api_key, config.api_key_length, workspace.plaintext,
                workspace.plaintext_capacity, pos))
    return Status::TooLarge;
  return save_record(storage_, random_, kProviderPath, sizeof(kProviderPath) - 1,
                     core_security::RecordType::AiProvider, workspace, pos, generation);
}

// Collection records share a compact wire layout: magic, count, then bounded
// key length/key, content length/content, and (for prompts) a flags byte.
template <bool Prompt>
Status visit_collection(core_security::StorageService& storage, SemaphoreHandle_t mutex,
                        const char* path, std::size_t path_len, core_security::RecordType type,
                        const std::uint8_t (&magic)[4], std::size_t payload_capacity,
                        bool (*callback)(const char*, const std::uint8_t*, std::size_t, bool,
                                         void*),
                        void* context) {
  Lock lock(mutex);
  if (!lock.held)
    return Status::StorageError;
  Workspace workspace(payload_capacity);
  if (!workspace.valid())
    return Status::StorageError;
  std::size_t size{};
  std::uint64_t generation{};
  Status result = load_record(storage, path, path_len, type, workspace, size, generation);
  if (result != Status::Ok)
    return result;
  if (size < 5 || std::memcmp(workspace.plaintext, magic, 4) != 0)
    return Status::CorruptData;
  std::size_t pos = 4;
  std::uint8_t count{};
  if (!get8(workspace.plaintext, size, pos, count) ||
      count > (Prompt ? kMaxPromptFiles : kMaxMemorySections))
    return Status::CorruptData;
  for (std::size_t index = 0; index < count; ++index) {
    std::uint16_t key_size{};
    std::uint32_t content_size{};
    const std::uint8_t* key{};
    const std::uint8_t* content{};
    std::uint8_t flags{};
    const std::size_t key_limit = Prompt ? kMaxPromptPathBytes : kMaxMemoryIdBytes;
    const std::size_t content_limit = Prompt ? kMaxPromptMarkdownBytes : kMaxMemoryMarkdownBytes;
    if (!get16(workspace.plaintext, size, pos, key_size) || key_size == 0 || key_size > key_limit ||
        !take(workspace.plaintext, size, pos, key_size, key) ||
        !get32(workspace.plaintext, size, pos, content_size) || content_size > content_limit ||
        !take(workspace.plaintext, size, pos, content_size, content) ||
        (Prompt && !get8(workspace.plaintext, size, pos, flags)))
      return Status::CorruptData;
    char key_text[kMaxPromptPathBytes + 1]{};
    std::memcpy(key_text, key, key_size);
    if (!(Prompt ? valid_path(key_text) : valid_id(key_text)))
      return Status::CorruptData;
    if (callback && !callback(key_text, content, content_size, (flags & 1U) != 0, context))
      return Status::Ok;
  }
  return pos == size ? Status::Ok : Status::CorruptData;
}

template <bool Prompt>
Status upsert_collection(core_security::StorageService& storage,
                         core_security::RandomSource& random, SemaphoreHandle_t mutex,
                         const char* path, std::size_t path_len, core_security::RecordType type,
                         const std::uint8_t (&magic)[4], std::size_t payload_capacity,
                         const char* key, const std::uint8_t* content, std::size_t content_size,
                         bool baseline) {
  const std::size_t key_limit = Prompt ? kMaxPromptPathBytes : kMaxMemoryIdBytes;
  const std::size_t content_limit = Prompt ? kMaxPromptMarkdownBytes : kMaxMemoryMarkdownBytes;
  if (!(Prompt ? valid_path(key) : valid_id(key)) || (!content && content_size))
    return Status::InvalidArgument;
  if (content_size > content_limit)
    return Status::TooLarge;
  Lock lock(mutex);
  if (!lock.held)
    return Status::StorageError;
  Workspace source(payload_capacity), target(payload_capacity);
  if (!source.valid() || !target.valid())
    return Status::StorageError;
  std::size_t size{};
  std::uint64_t generation{};
  Status loaded = load_record(storage, path, path_len, type, source, size, generation);
  std::size_t source_pos = 0;
  std::uint8_t count = 0;
  if (loaded == Status::NotFound) {
    std::memcpy(source.plaintext, magic, 4);
    source.plaintext[4] = 0;
    size = 5;
  } else if (loaded != Status::Ok)
    return loaded;
  if (size < 5 || std::memcmp(source.plaintext, magic, 4) != 0)
    return Status::CorruptData;
  source_pos = 4;
  if (!get8(source.plaintext, size, source_pos, count) ||
      count > (Prompt ? kMaxPromptFiles : kMaxMemorySections))
    return Status::CorruptData;
  std::size_t target_pos = 5;
  std::uint8_t new_count = count;
  bool replaced = false;
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t entry_start = source_pos;
    std::uint16_t key_size{};
    std::uint32_t old_content_size{};
    const std::uint8_t* old_key{};
    const std::uint8_t* old_content{};
    std::uint8_t old_flags{};
    if (!get16(source.plaintext, size, source_pos, key_size) || key_size == 0 ||
        key_size > key_limit || !take(source.plaintext, size, source_pos, key_size, old_key) ||
        !get32(source.plaintext, size, source_pos, old_content_size) ||
        old_content_size > content_limit ||
        !take(source.plaintext, size, source_pos, old_content_size, old_content) ||
        (Prompt && !get8(source.plaintext, size, source_pos, old_flags)))
      return Status::CorruptData;
    const std::size_t requested = std::strlen(key);
    if (requested == key_size && std::memcmp(key, old_key, key_size) == 0) {
      if (!put16(static_cast<std::uint16_t>(requested), target.plaintext, target.plaintext_capacity,
                 target_pos) ||
          !putBytes(key, requested, target.plaintext, target.plaintext_capacity, target_pos) ||
          !put32(static_cast<std::uint32_t>(content_size), target.plaintext,
                 target.plaintext_capacity, target_pos) ||
          !putBytes(content, content_size, target.plaintext, target.plaintext_capacity,
                    target_pos) ||
          (Prompt &&
           !put8(baseline ? 1 : 0, target.plaintext, target.plaintext_capacity, target_pos)))
        return Status::TooLarge;
      replaced = true;
    } else if (!putBytes(source.plaintext + entry_start, source_pos - entry_start, target.plaintext,
                         target.plaintext_capacity, target_pos))
      return Status::TooLarge;
  }
  if (source_pos != size)
    return Status::CorruptData;
  if (!replaced) {
    const std::size_t limit = Prompt ? kMaxPromptFiles : kMaxMemorySections;
    if (count >= limit)
      return Status::CapacityExceeded;
    ++new_count;
    const std::size_t requested = std::strlen(key);
    if (!put16(static_cast<std::uint16_t>(requested), target.plaintext, target.plaintext_capacity,
               target_pos) ||
        !putBytes(key, requested, target.plaintext, target.plaintext_capacity, target_pos) ||
        !put32(static_cast<std::uint32_t>(content_size), target.plaintext,
               target.plaintext_capacity, target_pos) ||
        !putBytes(content, content_size, target.plaintext, target.plaintext_capacity, target_pos) ||
        (Prompt &&
         !put8(baseline ? 1 : 0, target.plaintext, target.plaintext_capacity, target_pos)))
      return Status::TooLarge;
  }
  std::memcpy(target.plaintext, magic, 4);
  target.plaintext[4] = new_count;
  return save_record(storage, random, path, path_len, type, target, target_pos, generation);
}

MemoryRepository::MemoryRepository(core_security::StorageService& storage,
                                   core_security::RandomSource& random)
    : storage_(storage), random_(random) {}
MemoryRepository::~MemoryRepository() {
  CommonState* common = state_;
  destroy_state(common);
  state_ = nullptr;
}
Status MemoryRepository::start() {
  CommonState* common = state_;
  Status result = start_state(common);
  state_ = static_cast<State*>(common);
  return result;
}
namespace {
struct MemoryVisitContext final {
  MemoryVisitor visitor;
  void* context;
};

bool memory_adapter(const char* key, const std::uint8_t* content, std::size_t size, bool,
                    void* raw) {
  const auto& context = *static_cast<MemoryVisitContext*>(raw);
  MemorySectionView view{key, content, size};
  return context.visitor(view, context.context);
}
struct MemoryGetContext {
  const char* id;
  std::uint8_t* output;
  std::size_t capacity;
  std::size_t* size;
  Status result{Status::NotFound};
};
bool memory_get(const char* key, const std::uint8_t* content, std::size_t size, bool, void* raw) {
  auto& context = *static_cast<MemoryGetContext*>(raw);
  if (std::strcmp(key, context.id) != 0)
    return true;
  *context.size = size;
  if (size > context.capacity)
    context.result = Status::TooLarge;
  else {
    if (size)
      std::memcpy(context.output, content, size);
    context.result = Status::Ok;
  }
  return false;
}
} // namespace
Status MemoryRepository::visit(MemoryVisitor visitor, void* context) {
  if (!state_)
    return Status::NotReady;
  if (!visitor)
    return Status::InvalidArgument;
  MemoryVisitContext visit_context{visitor, context};
  return visit_collection<false>(storage_, state_->mutex, kMemoryPath, sizeof(kMemoryPath) - 1,
                                 core_security::RecordType::AiMemory, kMemoryMagic, kMemoryPayload,
                                 memory_adapter, &visit_context);
}
Status MemoryRepository::get(const char* id, std::uint8_t* markdown, std::size_t capacity,
                             std::size_t& markdown_size) {
  markdown_size = 0;
  if (!state_)
    return Status::NotReady;
  if (!valid_id(id) || (!markdown && capacity))
    return Status::InvalidArgument;
  MemoryGetContext context{id, markdown, capacity, &markdown_size};
  Status result = visit_collection<false>(
      storage_, state_->mutex, kMemoryPath, sizeof(kMemoryPath) - 1,
      core_security::RecordType::AiMemory, kMemoryMagic, kMemoryPayload, memory_get, &context);
  return result == Status::Ok ? context.result : result;
}
Status MemoryRepository::upsert(const char* id, const std::uint8_t* markdown,
                                std::size_t markdown_size) {
  if (!state_)
    return Status::NotReady;
  return upsert_collection<false>(storage_, random_, state_->mutex, kMemoryPath,
                                  sizeof(kMemoryPath) - 1, core_security::RecordType::AiMemory,
                                  kMemoryMagic, kMemoryPayload, id, markdown, markdown_size, false);
}

PromptRepository::PromptRepository(core_security::StorageService& storage,
                                   core_security::RandomSource& random)
    : storage_(storage), random_(random) {}
PromptRepository::~PromptRepository() {
  CommonState* common = state_;
  destroy_state(common);
  state_ = nullptr;
}
Status PromptRepository::start() {
  CommonState* common = state_;
  Status result = start_state(common);
  state_ = static_cast<State*>(common);
  return result;
}
namespace {
struct PromptVisitContext final {
  PromptVisitor visitor;
  void* context;
};

bool prompt_adapter(const char* key, const std::uint8_t* content, std::size_t size, bool baseline,
                    void* raw) {
  const auto& context = *static_cast<PromptVisitContext*>(raw);
  PromptFileView view{key, content, size, baseline};
  return context.visitor(view, context.context);
}
struct PromptGetContext {
  const char* path;
  std::uint8_t* output;
  std::size_t capacity;
  std::size_t* size;
  bool* baseline;
  Status result{Status::NotFound};
};
bool prompt_get(const char* key, const std::uint8_t* content, std::size_t size, bool baseline,
                void* raw) {
  auto& context = *static_cast<PromptGetContext*>(raw);
  if (std::strcmp(key, context.path) != 0)
    return true;
  *context.size = size;
  *context.baseline = baseline;
  if (size > context.capacity)
    context.result = Status::TooLarge;
  else {
    if (size)
      std::memcpy(context.output, content, size);
    context.result = Status::Ok;
  }
  return false;
}
} // namespace
Status PromptRepository::visit(PromptVisitor visitor, void* context) {
  if (!state_)
    return Status::NotReady;
  if (!visitor)
    return Status::InvalidArgument;
  PromptVisitContext visit_context{visitor, context};
  return visit_collection<true>(storage_, state_->mutex, kPromptsPath, sizeof(kPromptsPath) - 1,
                                core_security::RecordType::AiPrompts, kPromptMagic, kPromptPayload,
                                prompt_adapter, &visit_context);
}
Status PromptRepository::get(const char* path, std::uint8_t* markdown, std::size_t capacity,
                             std::size_t& markdown_size, bool& baseline) {
  markdown_size = 0;
  baseline = false;
  if (!state_)
    return Status::NotReady;
  if (!valid_path(path) || (!markdown && capacity))
    return Status::InvalidArgument;
  PromptGetContext context{path, markdown, capacity, &markdown_size, &baseline};
  Status result = visit_collection<true>(
      storage_, state_->mutex, kPromptsPath, sizeof(kPromptsPath) - 1,
      core_security::RecordType::AiPrompts, kPromptMagic, kPromptPayload, prompt_get, &context);
  return result == Status::Ok ? context.result : result;
}
Status PromptRepository::upsert(const char* path, const std::uint8_t* markdown,
                                std::size_t markdown_size, bool baseline) {
  if (!state_)
    return Status::NotReady;
  return upsert_collection<true>(storage_, random_, state_->mutex, kPromptsPath,
                                 sizeof(kPromptsPath) - 1, core_security::RecordType::AiPrompts,
                                 kPromptMagic, kPromptPayload, path, markdown, markdown_size,
                                 baseline);
}

AiStorageService::AiStorageService(core_security::StorageService& storage,
                                   core_security::RandomSource& random)
    : provider(storage, random), memory(storage, random), prompts(storage, random) {}
Status AiStorageService::start() {
  Status result = provider.start();
  if (result != Status::Ok)
    return result;
  result = memory.start();
  return result == Status::Ok ? prompts.start() : result;
}

} // namespace core_pilot::ai
