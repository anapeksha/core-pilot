#include "ai_chat_service.hpp"

#include "cJSON.h"
#include "core_domain.hpp"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "http_server.hpp"
#include "mbedtls/platform_util.h"
#include "mcp_service.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace core_pilot {
namespace {

constexpr std::size_t kRequestJsonBytes = std::size_t{256} * 1024;
constexpr std::size_t kResponseJsonBytes = std::size_t{64} * 1024;
constexpr std::size_t kSystemContextBytes = std::size_t{64} * 1024;
constexpr std::size_t kEndpointBytes = ai::kMaxProviderUrlBytes + 32;
constexpr int kHttpBufferBytes = 2048;
constexpr std::size_t kMaxToolNameBytes = 64;
constexpr std::size_t kMaxToolArgumentsBytes = 4096;

constexpr std::uint8_t kMaxIdenticalCalls = 3;

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

bool print_bounded(const cJSON* value, char* output, std::size_t capacity) {
  return value != nullptr && output != nullptr && capacity > 1 &&
         cJSON_PrintPreallocated(const_cast<cJSON*>(value), output, static_cast<int>(capacity),
                                 false);
}

cJSON* provider_tools(ai::ProviderBackend backend) {
  cJSON* registry = McpService::tools();
  const cJSON* tools =
      registry == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(registry, "tools");
  cJSON* result = cJSON_CreateArray();
  const int count = cJSON_IsArray(tools) ? cJSON_GetArraySize(tools) : 0;
  for (int index = 0; result != nullptr && index < count; ++index) {
    const cJSON* descriptor = cJSON_GetArrayItem(tools, index);
    const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(descriptor, "enabled");
    const cJSON* available = cJSON_GetObjectItemCaseSensitive(descriptor, "available");
    const cJSON* name = cJSON_GetObjectItemCaseSensitive(descriptor, "name");
    const cJSON* description = cJSON_GetObjectItemCaseSensitive(descriptor, "description");
    const cJSON* schema = cJSON_GetObjectItemCaseSensitive(descriptor, "inputSchema");
    if (!cJSON_IsTrue(enabled) || !cJSON_IsTrue(available) || !cJSON_IsString(name) ||
        std::strlen(name->valuestring) > kMaxToolNameBytes || !cJSON_IsObject(schema))
      continue;
    cJSON* item = cJSON_CreateObject();
    cJSON* schema_copy = cJSON_Duplicate(schema, true);
    if (item == nullptr || schema_copy == nullptr) {
      cJSON_Delete(item);
      cJSON_Delete(schema_copy);
      cJSON_Delete(result);
      result = nullptr;
      break;
    }
    if (backend == ai::ProviderBackend::OpenAiCompatible) {
      cJSON_AddStringToObject(item, "type", "function");
      cJSON* function = cJSON_AddObjectToObject(item, "function");
      cJSON_AddStringToObject(function, "name", name->valuestring);
      if (cJSON_IsString(description))
        cJSON_AddStringToObject(function, "description", description->valuestring);
      cJSON_AddItemToObject(function, "parameters", schema_copy);
    } else {
      cJSON_AddStringToObject(item, "name", name->valuestring);
      if (cJSON_IsString(description))
        cJSON_AddStringToObject(item, "description", description->valuestring);
      cJSON_AddItemToObject(item, "input_schema", schema_copy);
    }
    cJSON_AddItemToArray(result, item);
  }
  cJSON_Delete(registry);
  return result;
}

bool same_call(const char* left_tool, const char* left_arguments, const char* right_tool,
               const char* right_arguments) {
  return std::strcmp(left_tool, right_tool) == 0 &&
         std::strcmp(left_arguments, right_arguments) == 0;
}

struct SystemContextBuilder final {
  char* output;
  std::size_t capacity;
  std::size_t length{};
  bool valid{true};
};

bool append_context(SystemContextBuilder& builder, const char* label, const char* key,
                    const std::uint8_t* content, std::size_t content_size) {
  if (!builder.valid || label == nullptr || key == nullptr ||
      (content == nullptr && content_size != 0))
    return false;
  const int header_length =
      std::snprintf(builder.output + builder.length, builder.capacity - builder.length,
                    "\n\n[%s:%s]\n", label, key);
  if (header_length <= 0 ||
      static_cast<std::size_t>(header_length) >= builder.capacity - builder.length ||
      content_size >= builder.capacity - builder.length - static_cast<std::size_t>(header_length)) {
    builder.valid = false;
    return false;
  }
  builder.length += static_cast<std::size_t>(header_length);
  if (content_size != 0)
    std::memcpy(builder.output + builder.length, content, content_size);
  builder.length += content_size;
  builder.output[builder.length] = '\0';
  return true;
}

bool append_prompt(const ai::PromptFileView& prompt, void* context) {
  return append_context(*static_cast<SystemContextBuilder*>(context), "prompt", prompt.path,
                        prompt.markdown, prompt.markdown_size);
}

bool append_memory(const ai::MemorySectionView& memory, void* context) {
  return append_context(*static_cast<SystemContextBuilder*>(context), "memory", memory.id,
                        memory.markdown, memory.markdown_size);
}

} // namespace

struct AiChatService::RequestSlot final {
  struct StoredHistory final {
    Role role{Role::user};
    std::size_t length{};
    char content[kMaxHistoryContentBytes + 1]{};
  };
  std::uint64_t operation_id{};
  std::size_t message_length{};
  char message[kMaxMessageBytes + 1]{};
  std::size_t history_count{};
  StoredHistory history[kMaxHistoryEntries]{};
};

struct AiChatService::Runtime final {
  Snapshot snapshot{};
  Snapshot work{};
  char prior_tools[kMaxActions][kMaxToolNameBytes + 1]{};
  char prior_arguments[kMaxActions][kMaxToolArgumentsBytes + 1]{};
  char result[kMaxResponseBytes + 1]{};
  char system_context[kSystemContextBytes]{};
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

void AiChatService::bindRuntime(const HttpRuntime& runtime) {
  http_runtime_ = &runtime;
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

bool AiChatService::status(std::uint64_t operation_id, Snapshot& output, char* response,
                           std::size_t response_capacity) const {
  Lock lock(mutex_);
  if (!lock.held() || runtime_ == nullptr || operation_id == 0 ||
      runtime_->snapshot.operation_id != operation_id)
    return false;
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
    mbedtls_platform_zeroize(runtime_->system_context, sizeof runtime_->system_context);
    mbedtls_platform_zeroize(runtime_->request_json, sizeof runtime_->request_json);
    mbedtls_platform_zeroize(runtime_->response_json, sizeof runtime_->response_json);
  }
}

void AiChatService::process(std::uint64_t operation_id) {
  mbedtls_platform_zeroize(&runtime_->work, sizeof runtime_->work);
  mbedtls_platform_zeroize(runtime_->prior_tools, sizeof runtime_->prior_tools);
  mbedtls_platform_zeroize(runtime_->prior_arguments, sizeof runtime_->prior_arguments);
  Snapshot& result = runtime_->work;
  result.state = State::failed;
  result.operation_id = operation_id;
  char* const answer = runtime_->result;
  answer[0] = '\0';

  ai::ProviderConfig provider{};
  struct ProviderWipe final {
    ai::ProviderConfig& value;
    ~ProviderWipe() {
      ai::clear_provider_config(value);
    }
  } provider_wipe{provider};

  cJSON* request_root = nullptr;
  const char* failure_detail = nullptr;
  Error failure_error = Error::internal;
  std::size_t prior_count = 0;

  do {
    if (storage_.provider.load(provider) != ai::Status::Ok) {
      failure_error = Error::not_configured;
      failure_detail = "provider configuration unavailable";
      break;
    }
    if (!ai::provider_config_usable(provider)) {
      failure_error = Error::unsafe_url;
      failure_detail = "provider configuration is not supported or usable";
      break;
    }
    if (http_runtime_ == nullptr) {
      failure_error = Error::actions_unsupported;
      failure_detail = "AI actions require a bound HTTP runtime";
      break;
    }

    const char* suffix = provider.backend == ai::ProviderBackend::OpenAiCompatible
                             ? "/chat/completions"
                             : "/messages";
    const std::size_t base_length = std::strlen(provider.base_url);
    const bool trailing_slash = base_length != 0 && provider.base_url[base_length - 1] == '/';
    const int endpoint_length =
        std::snprintf(runtime_->endpoint, sizeof runtime_->endpoint, "%s%s", provider.base_url,
                      trailing_slash ? suffix + 1 : suffix);
    if (endpoint_length <= 0 ||
        static_cast<std::size_t>(endpoint_length) >= sizeof runtime_->endpoint) {
      failure_error = Error::invalid_provider;
      failure_detail = "provider endpoint is too long";
      break;
    }

    request_root = cJSON_CreateObject();
    cJSON* messages = cJSON_CreateArray();
    bool valid =
        request_root != nullptr && messages != nullptr &&
        cJSON_AddStringToObject(request_root, "model", provider.model) != nullptr &&
        cJSON_AddNumberToObject(request_root, "max_tokens", provider.max_output_tokens) != nullptr;
    if (valid && provider.has_temperature)
      valid = cJSON_AddNumberToObject(request_root, "temperature", provider.temperature) != nullptr;
    if (valid)
      cJSON_AddItemToObject(request_root, "messages", messages);
    else
      cJSON_Delete(messages);
    if (valid) {
      mbedtls_platform_zeroize(runtime_->system_context, sizeof runtime_->system_context);
      SystemContextBuilder builder{runtime_->system_context, sizeof runtime_->system_context};
      cJSON* context = McpService::context(*http_runtime_);
      const cJSON* system_prompt =
          context == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(context, "system_prompt");
      const cJSON* context_json =
          context == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(context, "context_json");
      valid = cJSON_IsString(system_prompt) && cJSON_IsString(context_json) &&
              append_context(builder, "core", "instructions",
                             reinterpret_cast<const std::uint8_t*>(system_prompt->valuestring),
                             std::strlen(system_prompt->valuestring));
      const ai::Status prompts_status =
          valid ? storage_.prompts.visit(append_prompt, &builder) : ai::Status::StorageError;
      const ai::Status memory_status =
          valid ? storage_.memory.visit(append_memory, &builder) : ai::Status::StorageError;
      valid = valid && builder.valid &&
              (prompts_status == ai::Status::Ok || prompts_status == ai::Status::NotFound) &&
              (memory_status == ai::Status::Ok || memory_status == ai::Status::NotFound) &&
              append_context(builder, "core", "live-context",
                             reinterpret_cast<const std::uint8_t*>(context_json->valuestring),
                             std::strlen(context_json->valuestring));
      if (!valid) {
        failure_error = Error::internal;
        failure_detail = builder.valid ? "AI prompt or memory storage is unreadable"
                                       : "AI prompt and memory context exceeds 64 KiB";
      } else if (provider.backend == ai::ProviderBackend::OpenAiCompatible) {
        valid = add_message(messages, "system", builder.output, builder.length);
      } else {
        valid = cJSON_AddStringToObject(request_root, "system", builder.output) != nullptr;
      }
      cJSON_Delete(context);
    }
    for (std::size_t index = 0; valid && index < request_->history_count; ++index) {
      const char* role = role_name(request_->history[index].role);
      if (provider.backend == ai::ProviderBackend::AnthropicCompatible &&
          request_->history[index].role == Role::tool)
        role = "user";
      valid = add_message(messages, role, request_->history[index].content,
                          request_->history[index].length);
    }
    valid = valid && add_message(messages, "user", request_->message, request_->message_length);
    if (valid) {
      cJSON* tools = provider_tools(provider.backend);
      valid = tools != nullptr;
      if (valid && cJSON_GetArraySize(tools) != 0)
        cJSON_AddItemToObject(request_root, "tools", tools);
      else
        cJSON_Delete(tools);
    }
    if (!valid) {
      if (failure_detail == nullptr) {
        failure_error = Error::json_encode;
        failure_detail = "provider request allocation failed";
      }
      break;
    }

    bool complete = false;
    for (std::uint8_t round = 0; round <= core_domain::max_ai_tool_rounds && !complete; ++round) {
      mbedtls_platform_zeroize(runtime_->request_json, sizeof runtime_->request_json);
      mbedtls_platform_zeroize(runtime_->response_json, sizeof runtime_->response_json);
      if (!print_bounded(request_root, runtime_->request_json, sizeof runtime_->request_json)) {
        failure_error = Error::json_encode;
        failure_detail = "request JSON exceeds the bounded workspace";
        break;
      }

      char auth_header[sizeof("Bearer ") + ai::kMaxApiKeyBytes]{};
      bool key_valid = provider.api_key_length <= ai::kMaxApiKeyBytes;
      for (std::size_t index = 0; key_valid && index < provider.api_key_length; ++index)
        key_valid = provider.api_key[index] > 0x20 && provider.api_key[index] != 0x7f;
      if (!key_valid ||
          (provider.auth_type != ai::AuthType::None && provider.api_key_length == 0)) {
        mbedtls_platform_zeroize(auth_header, sizeof auth_header);
        failure_error = Error::invalid_provider;
        failure_detail = "provider authentication is invalid";
        break;
      }
      HttpAccumulator accumulator{runtime_->response_json, sizeof runtime_->response_json, 0,
                                  false};
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
        mbedtls_platform_zeroize(auth_header, sizeof auth_header);
        failure_error = Error::internal;
        failure_detail = "HTTP client allocation failed";
        break;
      }
      esp_http_client_set_method(client, HTTP_METHOD_POST);
      esp_http_client_set_header(client, "Content-Type", "application/json");
      if (provider.auth_type == ai::AuthType::Bearer) {
        std::memcpy(auth_header, "Bearer ", 7);
        std::memcpy(auth_header + 7, provider.api_key, provider.api_key_length);
        esp_http_client_set_header(client, "Authorization", auth_header);
      } else if (provider.auth_type == ai::AuthType::ApiKey) {
        std::memcpy(auth_header, provider.api_key, provider.api_key_length);
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
        failure_error = Error::response_too_large;
        failure_detail = "provider response exceeds the transport limit";
        break;
      }
      if (performed != ESP_OK) {
        failure_error = socket_errno == ETIMEDOUT ? Error::timeout : Error::network;
        failure_detail =
            socket_errno == ETIMEDOUT ? "provider request timed out" : "provider connection failed";
        break;
      }
      if (status_code < 200 || status_code >= 300) {
        failure_error = Error::http_status;
        failure_detail = "provider returned a non-success HTTP status";
        break;
      }

      cJSON* response = cJSON_ParseWithLength(runtime_->response_json, accumulator.length);
      if (response == nullptr) {
        failure_error = Error::invalid_response;
        failure_detail = "provider returned invalid JSON";
        break;
      }
      const cJSON* assistant_message = nullptr;
      const cJSON* content = nullptr;
      const cJSON* calls = nullptr;
      if (provider.backend == ai::ProviderBackend::OpenAiCompatible) {
        const cJSON* choices = cJSON_GetObjectItemCaseSensitive(response, "choices");
        const cJSON* choice = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : nullptr;
        assistant_message = cJSON_GetObjectItemCaseSensitive(choice, "message");
        content = cJSON_GetObjectItemCaseSensitive(assistant_message, "content");
        calls = cJSON_GetObjectItemCaseSensitive(assistant_message, "tool_calls");
      } else {
        content = cJSON_GetObjectItemCaseSensitive(response, "content");
        calls = content;
      }

      int call_count = 0;
      if (cJSON_IsArray(calls)) {
        const int candidate_count = cJSON_GetArraySize(calls);
        for (int index = 0; index < candidate_count; ++index) {
          const cJSON* call = cJSON_GetArrayItem(calls, index);
          const cJSON* type = cJSON_GetObjectItemCaseSensitive(call, "type");
          if (provider.backend == ai::ProviderBackend::OpenAiCompatible ||
              (cJSON_IsString(type) && std::strcmp(type->valuestring, "tool_use") == 0))
            ++call_count;
        }
      }
      if (call_count == 0) {
        std::size_t answer_length = 0;
        if (provider.backend == ai::ProviderBackend::OpenAiCompatible && cJSON_IsString(content)) {
          answer_length = std::strlen(content->valuestring);
          if (answer_length <= kMaxResponseBytes)
            std::memcpy(answer, content->valuestring, answer_length);
        } else if (provider.backend == ai::ProviderBackend::AnthropicCompatible &&
                   cJSON_IsArray(content)) {
          const int blocks = cJSON_GetArraySize(content);
          for (int index = 0; index < blocks; ++index) {
            const cJSON* block = cJSON_GetArrayItem(content, index);
            const cJSON* type = cJSON_GetObjectItemCaseSensitive(block, "type");
            const cJSON* text = cJSON_GetObjectItemCaseSensitive(block, "text");
            if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "text") != 0 ||
                !cJSON_IsString(text))
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
        cJSON_Delete(response);
        if (answer_length > kMaxResponseBytes) {
          failure_error = Error::response_too_large;
          failure_detail = "assistant response exceeds 16 KiB";
          break;
        }
        if (answer_length == 0) {
          failure_error = Error::invalid_response;
          failure_detail = "provider response contains no assistant text";
          break;
        }
        answer[answer_length] = '\0';
        result.state = State::complete;
        result.error = Error::none;
        result.response_length = answer_length;
        complete = true;
        continue;
      }
      if (round >= core_domain::max_ai_tool_rounds) {
        cJSON_Delete(response);
        failure_error = Error::actions_unsupported;
        failure_detail = "AI tool round limit exceeded";
        break;
      }
      if (call_count > static_cast<int>(kMaxActions - result.action_count)) {
        cJSON_Delete(response);
        failure_error = Error::actions_unsupported;
        failure_detail = "AI action limit exceeded";
        break;
      }

      cJSON* continuation_messages = cJSON_GetObjectItemCaseSensitive(request_root, "messages");
      cJSON* anthropic_results = provider.backend == ai::ProviderBackend::AnthropicCompatible
                                     ? cJSON_CreateArray()
                                     : nullptr;
      if (provider.backend == ai::ProviderBackend::OpenAiCompatible)
        cJSON_AddItemToArray(continuation_messages, cJSON_Duplicate(assistant_message, true));
      else {
        cJSON* assistant = cJSON_CreateObject();
        cJSON_AddStringToObject(assistant, "role", "assistant");
        cJSON_AddItemToObject(assistant, "content", cJSON_Duplicate(content, true));
        cJSON_AddItemToArray(continuation_messages, assistant);
      }

      bool calls_valid =
          anthropic_results != nullptr || provider.backend == ai::ProviderBackend::OpenAiCompatible;
      const int candidate_count = cJSON_GetArraySize(calls);
      for (int index = 0; calls_valid && index < candidate_count; ++index) {
        const cJSON* call = cJSON_GetArrayItem(calls, index);
        const cJSON* type = cJSON_GetObjectItemCaseSensitive(call, "type");
        if (provider.backend == ai::ProviderBackend::AnthropicCompatible &&
            (!cJSON_IsString(type) || std::strcmp(type->valuestring, "tool_use") != 0))
          continue;
        const cJSON* id = cJSON_GetObjectItemCaseSensitive(call, "id");
        const cJSON* function = provider.backend == ai::ProviderBackend::OpenAiCompatible
                                    ? cJSON_GetObjectItemCaseSensitive(call, "function")
                                    : call;
        const cJSON* name = cJSON_GetObjectItemCaseSensitive(function, "name");
        const cJSON* raw_arguments = cJSON_GetObjectItemCaseSensitive(
            function,
            provider.backend == ai::ProviderBackend::OpenAiCompatible ? "arguments" : "input");
        char arguments_json[kMaxToolArgumentsBytes + 1]{};
        cJSON* arguments = nullptr;
        if (cJSON_IsString(raw_arguments) &&
            std::strlen(raw_arguments->valuestring) <= kMaxToolArgumentsBytes) {
          std::memcpy(arguments_json, raw_arguments->valuestring,
                      std::strlen(raw_arguments->valuestring));
          arguments = cJSON_ParseWithLength(arguments_json, std::strlen(arguments_json));
        } else if (cJSON_IsObject(raw_arguments) &&
                   print_bounded(raw_arguments, arguments_json, sizeof arguments_json)) {
          arguments = cJSON_Duplicate(raw_arguments, true);
        }
        if (!cJSON_IsString(id) || std::strlen(id->valuestring) > 128 || !cJSON_IsString(name) ||
            std::strlen(name->valuestring) > kMaxToolNameBytes || !cJSON_IsObject(arguments)) {
          cJSON_Delete(arguments);
          mbedtls_platform_zeroize(arguments_json, sizeof arguments_json);
          calls_valid = false;
          failure_error = Error::invalid_response;
          failure_detail = "provider tool call is malformed or oversized";
          break;
        }
        std::uint8_t identical = 0;
        for (std::size_t prior = 0; prior < prior_count; ++prior)
          if (same_call(runtime_->prior_tools[prior], runtime_->prior_arguments[prior],
                        name->valuestring, arguments_json))
            ++identical;
        if (identical >= kMaxIdenticalCalls) {
          cJSON_Delete(arguments);
          mbedtls_platform_zeroize(arguments_json, sizeof arguments_json);
          calls_valid = false;
          failure_error = Error::actions_unsupported;
          failure_detail = "repeated identical AI tool call blocked";
          break;
        }
        std::snprintf(runtime_->prior_tools[prior_count], sizeof runtime_->prior_tools[prior_count],
                      "%s", name->valuestring);
        std::snprintf(runtime_->prior_arguments[prior_count],
                      sizeof runtime_->prior_arguments[prior_count], "%s", arguments_json);
        ++prior_count;

        cJSON* invocation = nullptr;
        const esp_err_t invoked =
            McpService::invoke(*http_runtime_, name->valuestring, arguments, invocation);
        cJSON_Delete(arguments);
        if (invocation == nullptr) {
          invocation = cJSON_CreateObject();
          cJSON_AddStringToObject(invocation, "status", "rejected");
          cJSON_AddNumberToObject(invocation, "error", invoked);
        }
        Action& action = result.actions[result.action_count++];
        std::snprintf(action.tool, sizeof action.tool, "%s", name->valuestring);
        if (!print_bounded(invocation, action.result_json, sizeof action.result_json)) {
          cJSON_Delete(invocation);
          mbedtls_platform_zeroize(arguments_json, sizeof arguments_json);
          calls_valid = false;
          failure_error = Error::response_too_large;
          failure_detail = "MCP tool result exceeds the bounded action record";
          break;
        }
        if (provider.backend == ai::ProviderBackend::OpenAiCompatible) {
          cJSON* tool_message = cJSON_CreateObject();
          cJSON_AddStringToObject(tool_message, "role", "tool");
          cJSON_AddStringToObject(tool_message, "tool_call_id", id->valuestring);
          cJSON_AddStringToObject(tool_message, "content", action.result_json);
          cJSON_AddItemToArray(continuation_messages, tool_message);
        } else {
          cJSON* tool_result = cJSON_CreateObject();
          cJSON_AddStringToObject(tool_result, "type", "tool_result");
          cJSON_AddStringToObject(tool_result, "tool_use_id", id->valuestring);
          cJSON_AddStringToObject(tool_result, "content", action.result_json);
          cJSON_AddItemToArray(anthropic_results, tool_result);
        }
        cJSON_Delete(invocation);
        mbedtls_platform_zeroize(arguments_json, sizeof arguments_json);
      }
      if (calls_valid && provider.backend == ai::ProviderBackend::AnthropicCompatible) {
        cJSON* user = cJSON_CreateObject();
        cJSON_AddStringToObject(user, "role", "user");
        cJSON_AddItemToObject(user, "content", anthropic_results);
        anthropic_results = nullptr;
        cJSON_AddItemToArray(continuation_messages, user);
      }
      cJSON_Delete(anthropic_results);
      cJSON_Delete(response);
      if (!calls_valid)
        break;
    }
  } while (false);

  cJSON_Delete(request_root);
  mbedtls_platform_zeroize(runtime_->prior_arguments, sizeof runtime_->prior_arguments);
  if (result.state != State::complete)
    set_failure(result, failure_error,
                failure_detail == nullptr ? "AI chat failed" : failure_detail);

  {
    Lock lock(mutex_);
    if (!lock.held() || runtime_->snapshot.operation_id != operation_id)
      return;
    if (result.state != State::complete)
      mbedtls_platform_zeroize(runtime_->result, sizeof runtime_->result);
    runtime_->snapshot = result;
  }
}

} // namespace core_pilot
