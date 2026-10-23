#pragma once

#include "core_security.hpp"

#include <cstddef>
#include <cstdint>

namespace core_pilot::ai {

inline constexpr std::size_t kMaxProviderUrlBytes = 2048;
inline constexpr std::size_t kMaxProviderModelBytes = 128;
inline constexpr std::size_t kMaxApiKeyBytes = 256;
inline constexpr std::size_t kMaxMemoryIdBytes = 48;
inline constexpr std::size_t kMaxMemoryMarkdownBytes = std::size_t{12} * 1024;
inline constexpr std::size_t kMaxMemorySections = 16;
inline constexpr std::size_t kMaxPromptPathBytes = 128;
inline constexpr std::size_t kMaxPromptMarkdownBytes = 4096;
inline constexpr std::size_t kMaxPromptFiles = 32;

// Service statuses are intentionally transport-neutral so HTTP integration can
// map them to the stable core_domain error codes without exposing storage data.
enum class Status : std::uint8_t {
  Ok,
  NotFound,
  InvalidArgument,
  TooLarge,
  CapacityExceeded,
  NotReady,
  StorageError,
  CorruptData,
  UnsupportedFormat,
};

enum class ProviderBackend : std::uint8_t { OpenAiCompatible, AnthropicCompatible };
enum class AuthType : std::uint8_t { Bearer, ApiKey, None };

struct ProviderConfig final {
  ProviderBackend backend{ProviderBackend::OpenAiCompatible};
  AuthType auth_type{AuthType::Bearer};
  char base_url[kMaxProviderUrlBytes + 1]{};
  char model[kMaxProviderModelBytes + 1]{};
  std::uint32_t max_output_tokens{1024};
  std::uint32_t timeout_ms{30000};
  float temperature{0.7F};
  bool has_temperature{true};
  bool api_key_redacted{false};
  std::uint16_t api_key_length{};
  std::uint8_t api_key[kMaxApiKeyBytes]{};
};

// Wipes the complete object, including padding and the API key.
void clear_provider_config(ProviderConfig& config);

struct MemorySectionView final {
  const char* id;
  const std::uint8_t* markdown;
  std::size_t markdown_size;
};
using MemoryVisitor = bool (*)(const MemorySectionView& section, void* context);

struct PromptFileView final {
  const char* path;
  const std::uint8_t* markdown;
  std::size_t markdown_size;
  bool baseline;
};
using PromptVisitor = bool (*)(const PromptFileView& file, void* context);

class ProviderConfigRepository final {
public:
  ProviderConfigRepository(core_security::StorageService& storage,
                           core_security::RandomSource& random);
  ~ProviderConfigRepository();
  ProviderConfigRepository(const ProviderConfigRepository&) = delete;
  ProviderConfigRepository& operator=(const ProviderConfigRepository&) = delete;

  Status start();
  Status load(ProviderConfig& output);
  // Consumes config.api_key: it is wiped on every return path. With
  // api_key_redacted=true and an empty key, the previously stored key is kept.
  Status save(ProviderConfig& config);

private:
  struct State;
  State* state_{nullptr};
  core_security::StorageService& storage_;
  core_security::RandomSource& random_;
};

class MemoryRepository final {
public:
  MemoryRepository(core_security::StorageService& storage, core_security::RandomSource& random);
  ~MemoryRepository();
  MemoryRepository(const MemoryRepository&) = delete;
  MemoryRepository& operator=(const MemoryRepository&) = delete;

  Status start();
  // Views are valid only for the duration of the callback and must not be retained.
  Status visit(MemoryVisitor visitor, void* context);
  Status get(const char* id, std::uint8_t* markdown, std::size_t capacity,
             std::size_t& markdown_size);
  Status upsert(const char* id, const std::uint8_t* markdown, std::size_t markdown_size);

private:
  struct State;
  State* state_{nullptr};
  core_security::StorageService& storage_;
  core_security::RandomSource& random_;
};

class PromptRepository final {
public:
  PromptRepository(core_security::StorageService& storage, core_security::RandomSource& random);
  ~PromptRepository();
  PromptRepository(const PromptRepository&) = delete;
  PromptRepository& operator=(const PromptRepository&) = delete;

  Status start();
  Status visit(PromptVisitor visitor, void* context);
  Status get(const char* path, std::uint8_t* markdown, std::size_t capacity,
             std::size_t& markdown_size, bool& baseline);
  Status upsert(const char* path, const std::uint8_t* markdown, std::size_t markdown_size,
                bool baseline = false);

private:
  struct State;
  State* state_{nullptr};
  core_security::StorageService& storage_;
  core_security::RandomSource& random_;
};

// Convenience owner for HTTP orchestration. It persists configuration only;
// external HTTPS/chat transport is deliberately outside this component.
class AiStorageService final {
public:
  AiStorageService(core_security::StorageService& storage, core_security::RandomSource& random);
  Status start();

  ProviderConfigRepository provider;
  MemoryRepository memory;
  PromptRepository prompts;
};

} // namespace core_pilot::ai
