#include "ai_chat_service.hpp"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "mbedtls/platform_util.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace core_pilot {
namespace {

constexpr std::size_t kRequestJsonBytes = std::size_t{256} * 1024;
constexpr std::size_t kResponseJsonBytes = std::size_t{64} * 1024;
constexpr std::size_t kEndpointBytes = ai::kMaxProviderUrlBytes + 32;
constexpr int kHttpBufferBytes = 2048;

class Lock final {
public:
  explicit Lock(SemaphoreHandle_t mutex)
      : mutex_(mutex), held_(mutex != nullptr && xSemaphoreTake(mutex, 0) == pdTRUE) {}
  ~Lock() {
    if (held_)
      xSemaphoreGive(mutex_);
  }
  bool held() const {
    return held_;
  }

private:
  SemaphoreHandle_t mutex_;
  bool held_;
};

bool valid_text(const char* text, std::size_t length, std::size_t maximum) {
  return text != nullptr && length != 0 && length <= maximum &&
         std::memchr(text, '\0', length) == nullptr;
}

bool ascii_equal(const char* value, std::size_t length, const char* expected) {
  const std::size_t expected_length = std::strlen(expected);
  if (length != expected_length)
    return false;
  for (std::size_t index = 0; index < length; ++index) {
    char character = value[index];
    if (character >= 'A' && character <= 'Z')
      character = static_cast<char>(character - 'A' + 'a');
    if (character != expected[index])
      return false;
  }
  return true;
}

bool ascii_suffix(const char* value, std::size_t length, const char* suffix) {
  const std::size_t suffix_length = std::strlen(suffix);
  return length >= suffix_length &&
         ascii_equal(value + length - suffix_length, suffix_length, suffix);
}

// This is deliberately checked before esp_http_client can perform DNS. DNS rebinding is still
// mitigated only by TLS hostname validation; this service does not connect to plaintext endpoints.
bool extract_url_host(const char* url, char* output, std::size_t capacity) {
  if (url == nullptr || output == nullptr || capacity == 0 || std::strncmp(url, "https://", 8) != 0)
    return false;
  const char* begin = url + 8;
  const char* end = std::strpbrk(begin, "/?#");
  if (end == nullptr)
    end = begin + std::strlen(begin);
  const char* colon = nullptr;
  for (const char* cursor = begin; cursor < end; ++cursor)
    if (*cursor == ':')
      colon = cursor;
  if (colon != nullptr)
    end = colon;
  const std::size_t length = static_cast<std::size_t>(end - begin);
  if (length == 0 || length >= capacity)
    return false;
  std::memcpy(output, begin, length);
  output[length] = '\0';
  return true;
}

bool public_ipv4(const in_addr& address) {
  const std::uint32_t value = ntohl(address.s_addr);
  const std::uint8_t first = static_cast<std::uint8_t>(value >> 24U);
  const std::uint8_t second = static_cast<std::uint8_t>(value >> 16U);
  if (first == 0 || first == 10 || first == 127 || first >= 224)
    return false;
  if (first == 100 && second >= 64 && second <= 127)
    return false;
  if (first == 169 && second == 254)
    return false;
  if (first == 172 && second >= 16 && second <= 31)
    return false;
  if (first == 192 && (second == 0 || second == 168))
    return false;
  if (first == 198 && (second == 18 || second == 19 || second == 51))
    return false;
  if (first == 203 && second == 0)
    return false;
  return true;
}

bool public_ipv6(const in6_addr& address) {
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(&address);
  bool unspecified = true;
  for (std::size_t index = 0; index < 16; ++index)
    unspecified = unspecified && bytes[index] == 0;
  const bool loopback = std::memcmp(bytes, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\1", 16) == 0;
  const bool unique_local = (bytes[0] & 0xfeU) == 0xfcU;
  const bool link_local = bytes[0] == 0xfeU && (bytes[1] & 0xc0U) == 0x80U;
  const bool multicast = bytes[0] == 0xffU;
  return !unspecified && !loopback && !unique_local && !link_local && !multicast;
}

bool public_dns_target(const char* url) {
  char host[254]{};
  if (!extract_url_host(url, host, sizeof host))
    return false;
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* addresses = nullptr;
  if (getaddrinfo(host, "443", &hints, &addresses) != 0 || addresses == nullptr)
    return false;
  bool found = false;
  bool all_public = true;
  for (const addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
    if (address->ai_family == AF_INET) {
      found = true;
      const auto* socket = reinterpret_cast<const sockaddr_in*>(address->ai_addr);
      all_public = all_public && public_ipv4(socket->sin_addr);
    } else if (address->ai_family == AF_INET6) {
      found = true;
      const auto* socket = reinterpret_cast<const sockaddr_in6*>(address->ai_addr);
      all_public = all_public && public_ipv6(socket->sin6_addr);
    }
  }
  freeaddrinfo(addresses);
  mbedtls_platform_zeroize(host, sizeof host);
  return found && all_public;
}

bool safe_https_base_url(const char* url) {
  if (url == nullptr || std::strncmp(url, "https://", 8) != 0)
    return false;
  const char* authority = url + 8;
  const char* authority_end = std::strpbrk(authority, "/?#");
  if (authority_end == nullptr)
    authority_end = authority + std::strlen(authority);
  if (authority == authority_end || std::strchr(authority, '@') != nullptr ||
      std::strchr(authority_end, '#') != nullptr)
    return false;

  const char* host = authority;
  const char* host_end = authority_end;
  const char* colon = nullptr;
  for (const char* cursor = authority; cursor < authority_end; ++cursor) {
    if (*cursor == '[' || *cursor == ']')
      return false; // All literal IPv6 destinations are forbidden.
    if (*cursor == ':')
      colon = cursor;
  }
  if (colon != nullptr) {
    if (static_cast<std::size_t>(authority_end - colon) != 4 ||
        std::memcmp(colon + 1, "443", 3) != 0)
      return false;
    host_end = colon;
  }
  const std::size_t host_length = static_cast<std::size_t>(host_end - host);
  if (host_length == 0 || std::memchr(host, '.', host_length) == nullptr)
    return false;
  bool only_ip_characters = true;
  for (const char* cursor = host; cursor < host_end; ++cursor) {
    const bool digit = *cursor >= '0' && *cursor <= '9';
    if (!digit && *cursor != '.')
      only_ip_characters = false;
    const bool valid = digit || (*cursor >= 'A' && *cursor <= 'Z') ||
                       (*cursor >= 'a' && *cursor <= 'z') || *cursor == '-' || *cursor == '.';
    if (!valid)
      return false;
  }
  if (only_ip_characters || ascii_equal(host, host_length, "localhost") ||
      ascii_suffix(host, host_length, ".localhost") || ascii_suffix(host, host_length, ".local") ||
      ascii_suffix(host, host_length, ".lan") || ascii_suffix(host, host_length, ".internal") ||
      ascii_suffix(host, host_length, ".home.arpa") ||
      ascii_equal(host, host_length, "metadata.google.internal"))
    return false;
  return true;
}

const char* role_name(AiChatService::Role role) {
  switch (role) {
  case AiChatService::Role::user:
    return "user";
  case AiChatService::Role::assistant:
    return "assistant";
  case AiChatService::Role::tool:
    return "tool";
  }
  return nullptr;
}

void set_failure(AiChatService::Snapshot& snapshot, AiChatService::Error error,
                 const char* detail) {
  snapshot.state = AiChatService::State::failed;
  snapshot.error = error;
  std::snprintf(snapshot.error_detail, sizeof snapshot.error_detail, "%s", detail);
  snapshot.response_length = 0;
  snapshot.action_count = 0;
}

struct HttpAccumulator final {
  char* data{};
  std::size_t capacity{};
  std::size_t length{};
  bool overflow{};
};

esp_err_t http_event(esp_http_client_event_t* event) {
  if (event == nullptr || event->user_data == nullptr)
    return ESP_OK;
  auto& accumulator = *static_cast<HttpAccumulator*>(event->user_data);
  if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0)
    return ESP_OK;
  const auto length = static_cast<std::size_t>(event->data_len);
  if (length > accumulator.capacity - accumulator.length - 1) {
    accumulator.overflow = true;
    return ESP_FAIL;
  }
  std::memcpy(accumulator.data + accumulator.length, event->data, length);
  accumulator.length += length;
  accumulator.data[accumulator.length] = '\0';
  return ESP_OK;
}

bool add_message(cJSON* messages, const char* role, const char* content,
                 std::size_t content_length) {
  cJSON* message = cJSON_CreateObject();
  if (message == nullptr || cJSON_AddStringToObject(message, "role", role) == nullptr) {
    cJSON_Delete(message);
    return false;
  }
  char* copy = static_cast<char*>(cJSON_malloc(content_length + 1));
  if (copy == nullptr) {
    cJSON_Delete(message);
    return false;
  }
  std::memcpy(copy, content, content_length);
  copy[content_length] = '\0';
  cJSON* content_json = cJSON_CreateString(copy);
  mbedtls_platform_zeroize(copy, content_length + 1);
  cJSON_free(copy);
  if (content_json == nullptr) {
    cJSON_Delete(message);
    return false;
  }
  cJSON_AddItemToObject(message, "content", content_json);
  cJSON_AddItemToArray(messages, message);
  return true;
}

} // namespace

struct AiChatService::RequestSlot final {
  struct StoredHistory final {
    Role role{Role::user};
    std::size_t length{};
    char content[kMaxHistoryContentBytes + 1]{};
  };
  std::uint64_t operation_id{};
  bool allow_actions{};
  std::size_t message_length{};
  char message[kMaxMessageBytes + 1]{};
  std::size_t history_count{};
  StoredHistory history[kMaxHistoryEntries]{};
};

struct AiChatService::Runtime final {
  Snapshot snapshot{};
  char result[kMaxResponseBytes + 1]{};
  char endpoint[kEndpointBytes]{};
  char request_json[kRequestJsonBytes]{};
  char response_json[kResponseJsonBytes]{};
};

AiChatService::AiChatService(ai::AiStorageService& storage) : storage_(storage) {}

AiChatService::~AiChatService() {
  if (task_ != nullptr)
    vTaskDelete(task_);
  if (request_ != nullptr) {
    mbedtls_platform_zeroize(request_, sizeof(*request_));
    heap_caps_free(request_);
  }
  if (runtime_ != nullptr) {
    mbedtls_platform_zeroize(runtime_, sizeof(*runtime_));
    heap_caps_free(runtime_);
  }
  if (task_stack_ != nullptr) {
    mbedtls_platform_zeroize(task_stack_, kTaskStackBytes);
    heap_caps_free(task_stack_);
  }
  if (queue_ != nullptr)
    vQueueDelete(queue_);
  if (mutex_ != nullptr)
    vSemaphoreDelete(mutex_);
}

esp_err_t AiChatService::start() {
  if (started_)
    return ESP_OK;
  mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
  queue_ = xQueueCreateStatic(kQueueDepth, sizeof(std::uint64_t), queue_buffer_, &queue_storage_);
  if (mutex_ == nullptr || queue_ == nullptr)
    return ESP_ERR_NO_MEM;
  started_ = true;
  return ESP_OK;
}

bool AiChatService::ensureWorker() {
  if (task_ != nullptr)
    return true;
  if (request_ == nullptr)
    request_ = static_cast<RequestSlot*>(
        heap_caps_calloc(1, sizeof(RequestSlot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (runtime_ == nullptr)
    runtime_ = static_cast<Runtime*>(
        heap_caps_calloc(1, sizeof(Runtime), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (task_stack_ == nullptr)
    task_stack_ = static_cast<StackType_t*>(
        heap_caps_calloc(kTaskStackBytes / sizeof(StackType_t), sizeof(StackType_t),
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (request_ == nullptr || runtime_ == nullptr || task_stack_ == nullptr)
    return false;
  task_ = xTaskCreateStaticPinnedToCore(taskEntry, "ai_chat", kTaskStackBytes / sizeof(StackType_t),
                                        this, 4, task_stack_, &task_storage_, 0);
  return task_ != nullptr;
}

AiChatService::SubmitResult AiChatService::submit(const Request& input,
                                                  std::uint64_t& operation_id) {
  operation_id = 0;
  if (!started_ || mutex_ == nullptr || queue_ == nullptr)
    return SubmitResult::not_ready;
  if (!valid_text(input.message, input.message_length, kMaxMessageBytes) ||
      input.history_count > kMaxHistoryEntries ||
      (input.history_count != 0 && input.history == nullptr))
    return input.message_length > kMaxMessageBytes ? SubmitResult::too_large
                                                   : SubmitResult::invalid;
  for (std::size_t index = 0; index < input.history_count; ++index) {
    const HistoryEntry& entry = input.history[index];
    if (role_name(entry.role) == nullptr ||
        !valid_text(entry.content, entry.content_length, kMaxHistoryContentBytes))
      return entry.content_length > kMaxHistoryContentBytes ? SubmitResult::too_large
                                                            : SubmitResult::invalid;
  }

  Lock lock(mutex_);
  if (!lock.held())
    return SubmitResult::busy;
  if (!ensureWorker())
    return SubmitResult::no_memory;
  if (runtime_->snapshot.state == State::queued || runtime_->snapshot.state == State::processing)
    return SubmitResult::busy;

  mbedtls_platform_zeroize(request_, sizeof(*request_));
  request_->operation_id = next_operation_id_++;
  if (request_->operation_id == 0)
    request_->operation_id = next_operation_id_++;
  request_->allow_actions = input.allow_actions;
  request_->message_length = input.message_length;
  std::memcpy(request_->message, input.message, input.message_length);
  request_->history_count = input.history_count;
  for (std::size_t index = 0; index < input.history_count; ++index) {
    request_->history[index].role = input.history[index].role;
    request_->history[index].length = input.history[index].content_length;
    std::memcpy(request_->history[index].content, input.history[index].content,
                input.history[index].content_length);
  }

  mbedtls_platform_zeroize(&runtime_->snapshot, sizeof runtime_->snapshot);
  mbedtls_platform_zeroize(runtime_->result, sizeof runtime_->result);
  runtime_->snapshot.state = State::queued;
  runtime_->snapshot.operation_id = request_->operation_id;
  operation_id = request_->operation_id;
  if (xQueueSend(queue_, &operation_id, 0) != pdTRUE) {
    set_failure(runtime_->snapshot, Error::internal, "queue unavailable");
    return SubmitResult::busy;
  }
  return SubmitResult::queued;
}

bool AiChatService::status(Snapshot& output, char* response, std::size_t response_capacity) const {
  Lock lock(mutex_);
  if (!lock.held())
    return false;
  if (runtime_ == nullptr) {
    output = {};
    if (response != nullptr && response_capacity != 0)
      response[0] = '\0';
    return true;
  }
  if (runtime_->snapshot.response_length != 0 &&
      (response == nullptr || response_capacity <= runtime_->snapshot.response_length))
    return false;
  output = runtime_->snapshot;
  if (response != nullptr && response_capacity != 0) {
    if (output.response_length != 0)
      std::memcpy(response, runtime_->result, output.response_length);
    response[output.response_length] = '\0';
  }
  return true;
}

void AiChatService::taskEntry(void* context) {
  static_cast<AiChatService*>(context)->taskLoop();
}

void AiChatService::taskLoop() {
  while (true) {
    std::uint64_t operation_id{};
    if (xQueueReceive(queue_, &operation_id, portMAX_DELAY) != pdTRUE)
      continue;
    {
      Lock lock(mutex_);
      if (!lock.held() || runtime_->snapshot.operation_id != operation_id)
        continue;
      runtime_->snapshot.state = State::processing;
    }
    process(operation_id);
    mbedtls_platform_zeroize(request_, sizeof(*request_));
    mbedtls_platform_zeroize(runtime_->endpoint, sizeof runtime_->endpoint);
    mbedtls_platform_zeroize(runtime_->request_json, sizeof runtime_->request_json);
    mbedtls_platform_zeroize(runtime_->response_json, sizeof runtime_->response_json);
  }
}

void AiChatService::process(std::uint64_t operation_id) {
  Snapshot result{};
  result.state = State::failed;
  result.operation_id = operation_id;
  // The answer lives in the PSRAM result slot; the processing snapshot always reports length zero,
  // so status() will not expose it until publication.
  char* const answer = runtime_->result;

  ai::ProviderConfig provider{};
  struct ProviderWipe final {
    ai::ProviderConfig& value;
    ~ProviderWipe() {
      ai::clear_provider_config(value);
    }
  } provider_wipe{provider};
  if (storage_.provider.load(provider) != ai::Status::Ok) {
    set_failure(result, Error::not_configured, "provider configuration unavailable");
    goto publish;
  }
  if (!safe_https_base_url(provider.base_url) || !public_dns_target(provider.base_url)) {
    set_failure(result, Error::unsafe_url,
                "provider URL does not resolve exclusively to public HTTPS addresses");
    goto publish;
  }

  {
    const char* suffix = provider.backend == ai::ProviderBackend::OpenAiCompatible
                             ? "/chat/completions"
                             : "/messages";
    const std::size_t base_length = std::strlen(provider.base_url);
    const bool trailing_slash = base_length != 0 && provider.base_url[base_length - 1] == '/';
    const int written = std::snprintf(runtime_->endpoint, sizeof runtime_->endpoint, "%s%s",
                                      provider.base_url, trailing_slash ? suffix + 1 : suffix);
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof runtime_->endpoint) {
      set_failure(result, Error::invalid_provider, "provider endpoint is too long");
      goto publish;
    }
  }

  {
    cJSON* root = cJSON_CreateObject();
    cJSON* messages = cJSON_CreateArray();
    bool valid = root != nullptr && messages != nullptr;
    if (valid) {
      cJSON_AddStringToObject(root, "model", provider.model);
      cJSON_AddNumberToObject(root, "max_tokens", provider.max_output_tokens);
      if (provider.has_temperature)
        cJSON_AddNumberToObject(root, "temperature", provider.temperature);
      cJSON_AddItemToObject(root, "messages", messages);
      for (std::size_t index = 0; valid && index < request_->history_count; ++index) {
        const char* role = role_name(request_->history[index].role);
        // Anthropic has no plain "tool" message role without a tool_use id. Treat caller-supplied
        // tool context as user context; no action is advertised or considered executed.
        if (provider.backend == ai::ProviderBackend::AnthropicCompatible &&
            request_->history[index].role == Role::tool)
          role = "user";
        valid = add_message(messages, role, request_->history[index].content,
                            request_->history[index].length);
      }
      valid = valid && add_message(messages, "user", request_->message, request_->message_length);
    }
    const bool encoded =
        valid && cJSON_PrintPreallocated(root, runtime_->request_json,
                                         static_cast<int>(sizeof runtime_->request_json), false);
    cJSON_Delete(root);
    if (!encoded) {
      set_failure(result, Error::json_encode, "request JSON exceeds the bounded workspace");
      goto publish;
    }
  }

  {
    char auth_header[sizeof("Bearer ") + ai::kMaxApiKeyBytes]{};
    struct AuthWipe final {
      char* value;
      std::size_t size;
      ~AuthWipe() {
        mbedtls_platform_zeroize(value, size);
      }
    } auth_wipe{auth_header, sizeof auth_header};
    bool key_valid = provider.api_key_length <= ai::kMaxApiKeyBytes;
    for (std::size_t index = 0; key_valid && index < provider.api_key_length; ++index)
      key_valid = provider.api_key[index] > 0x20 && provider.api_key[index] != 0x7f;
    if (!key_valid || (provider.auth_type != ai::AuthType::None && provider.api_key_length == 0)) {
      set_failure(result, Error::invalid_provider, "provider authentication is invalid");
      goto publish;
    }

    HttpAccumulator accumulator{runtime_->response_json, sizeof runtime_->response_json, 0, false};
    esp_http_client_config_t config{};
    config.url = runtime_->endpoint;
    config.event_handler = http_event;
    config.user_data = &accumulator;
    config.timeout_ms = static_cast<int>(provider.timeout_ms);
    config.buffer_size = kHttpBufferBytes;
    config.buffer_size_tx = kHttpBufferBytes;
    config.disable_auto_redirect = true;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.skip_cert_common_name_check = false;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
      set_failure(result, Error::internal, "HTTP client allocation failed");
      goto publish;
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (provider.auth_type == ai::AuthType::Bearer) {
      std::memcpy(auth_header, "Bearer ", 7);
      std::memcpy(auth_header + 7, provider.api_key, provider.api_key_length);
      auth_header[7 + provider.api_key_length] = '\0';
      esp_http_client_set_header(client, "Authorization", auth_header);
    } else if (provider.auth_type == ai::AuthType::ApiKey) {
      std::memcpy(auth_header, provider.api_key, provider.api_key_length);
      auth_header[provider.api_key_length] = '\0';
      esp_http_client_set_header(client, "x-api-key", auth_header);
    }
    if (provider.backend == ai::ProviderBackend::AnthropicCompatible)
      esp_http_client_set_header(client, "anthropic-version", "2023-06-01");
    esp_http_client_set_post_field(client, runtime_->request_json,
                                   static_cast<int>(std::strlen(runtime_->request_json)));
    const esp_err_t performed = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    const int socket_errno = esp_http_client_get_errno(client);
    esp_http_client_cleanup(client);
    mbedtls_platform_zeroize(auth_header, sizeof auth_header);
    if (accumulator.overflow) {
      set_failure(result, Error::response_too_large,
                  "provider response exceeds the transport limit");
      goto publish;
    }
    if (performed != ESP_OK) {
      set_failure(result, socket_errno == ETIMEDOUT ? Error::timeout : Error::network,
                  socket_errno == ETIMEDOUT ? "provider request timed out"
                                            : "provider connection failed");
      goto publish;
    }
    if (status_code < 200 || status_code >= 300) {
      set_failure(result, Error::http_status, "provider returned a non-success HTTP status");
      goto publish;
    }
  }

  {
    cJSON* root =
        cJSON_ParseWithLength(runtime_->response_json, std::strlen(runtime_->response_json));
    std::size_t answer_length = 0;
    if (root != nullptr && provider.backend == ai::ProviderBackend::OpenAiCompatible) {
      const cJSON* choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
      const cJSON* choice = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : nullptr;
      const cJSON* message = cJSON_GetObjectItemCaseSensitive(choice, "message");
      const cJSON* content = cJSON_GetObjectItemCaseSensitive(message, "content");
      if (cJSON_IsString(content) && content->valuestring != nullptr)
        answer_length = std::strlen(content->valuestring);
      if (answer_length <= kMaxResponseBytes && answer_length != 0)
        std::memcpy(answer, content->valuestring, answer_length);
    } else if (root != nullptr) {
      const cJSON* content = cJSON_GetObjectItemCaseSensitive(root, "content");
      const int count = cJSON_IsArray(content) ? cJSON_GetArraySize(content) : 0;
      for (int index = 0; index < count; ++index) {
        const cJSON* block = cJSON_GetArrayItem(content, index);
        const cJSON* type = cJSON_GetObjectItemCaseSensitive(block, "type");
        const cJSON* text = cJSON_GetObjectItemCaseSensitive(block, "text");
        if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "text") != 0 ||
            !cJSON_IsString(text) || text->valuestring == nullptr)
          continue;
        const std::size_t length = std::strlen(text->valuestring);
        if (length > kMaxResponseBytes - answer_length) {
          answer_length = kMaxResponseBytes + 1;
          break;
        }
        std::memcpy(answer + answer_length, text->valuestring, length);
        answer_length += length;
      }
    }
    cJSON_Delete(root);
    if (answer_length > kMaxResponseBytes) {
      set_failure(result, Error::response_too_large, "assistant response exceeds 16 KiB");
      goto publish;
    }
    if (answer_length == 0) {
      set_failure(result, Error::invalid_response, "provider response contains no assistant text");
      goto publish;
    }
    answer[answer_length] = '\0';
    result.state = State::complete;
    result.error = Error::none;
    if (request_->allow_actions)
      std::snprintf(result.error_detail, sizeof result.error_detail, "%s",
                    "actions unsupported; none executed");
    result.response_length = answer_length;
    result.action_count = 0;
  }

publish: {
  Lock lock(mutex_);
  if (!lock.held() || runtime_->snapshot.operation_id != operation_id)
    return;
  if (result.state != State::complete)
    mbedtls_platform_zeroize(runtime_->result, sizeof runtime_->result);
  runtime_->snapshot = result;
}
}

} // namespace core_pilot
