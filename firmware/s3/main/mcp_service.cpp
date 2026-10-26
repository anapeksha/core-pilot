#include "mcp_service.hpp"

#include "cJSON.h"
#include "core_matter.hpp"
#include "core_security.hpp"
#include "device_registry.hpp"
#include "http_server.hpp"
#include "mbedtls/platform_util.h"
#include "model_repository.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace core_pilot {
namespace {

struct Tool final {
  const char* id;
  const char* name;
  const char* title;
  const char* category;
  const char* description;
  bool available;
  bool destructive;
};

constexpr Tool kTools[] = {
    {"devices.list", "ListDevices", "List devices", "Devices",
     "List commissioned Matter endpoints and their discovered capabilities.", true, false},
    {"devices.get_state", "GetDeviceState", "Get device state", "Devices",
     "Read live Matter attributes from a commissioned endpoint.", false, false},
    {"devices.set_power", "SetOnOff", "Set power", "Devices",
     "Queue an On/Off command for a commissioned endpoint.", true, false},
    {"devices.set_level", "SetLevel", "Set level", "Devices",
     "Queue a level command for a compatible endpoint.", true, false},
    {"devices.set_color", "SetColor", "Set color", "Devices",
     "Set a color on a compatible endpoint.", false, false},
    {"devices.set_color_temperature", "SetColorTemperature", "Set color temperature", "Devices",
     "Queue a color-temperature command for a compatible endpoint.", true, false},
    {"scenes.activate", "ActivateScene", "Activate scene", "Scenes",
     "Run a stored scene asynchronously.", false, false},
    {"scenes.list", "ListScenes", "List scenes", "Scenes", "List stored scenes.", true, false},
    {"automations.list", "ListAutomations", "List automations", "Automations",
     "List stored automations.", true, false},
    {"automations.enable", "EnableAutomation", "Enable automation", "Automations",
     "Enable a stored automation.", false, false},
    {"automations.disable", "DisableAutomation", "Disable automation", "Automations",
     "Disable a stored automation.", false, false},
    {"automations.create_simple", "CreateSimpleAutomation", "Create automation", "Automations",
     "Create a bounded local automation.", false, false},
    {"hub.status", "GetHubStatus", "Get hub status", "Hub",
     "Read local network, Matter, and Thread readiness.", true, false},
    {"matter.get_operation", "GetMatterOperation", "Get Matter operation", "Matter",
     "Read the current or most recent Matter command result.", true, false},
    {"matter.remove_device", "RemoveMatterDevice", "Remove Matter device", "Matter",
     "Remove a commissioned node from the fabric after explicit confirmation.", true, true},
    {"matter.reset_fabric", "ResetMatterFabric", "Reset Matter fabric", "Matter",
     "Erase the controller fabric after explicit confirmation.", false, true},
    {"hub.restart", "RestartHub", "Restart hub", "System", "Restart Core Pilot.", false, true},
    {"hub.shutdown", "ShutdownHub", "Shut down hub", "System", "Shut down Core Pilot.", false,
     true},
    {"hub.factory_reset", "FactoryReset", "Factory reset", "System",
     "Erase all local configuration after explicit confirmation.", false, true},
};

constexpr char kPermissionsPath[] = "/core-pilot/ai/tool-permissions.enc";
constexpr std::uint16_t kPermissionsSchema = 1;
portMUX_TYPE permissions_lock = portMUX_INITIALIZER_UNLOCKED;
std::uint32_t enabled_tools = 0;
std::uint64_t permissions_generation = 0;

constexpr std::uint32_t availablePermissions() {
  std::uint32_t mask = 0;
  for (std::size_t index = 0; index < std::size(kTools); ++index)
    if (kTools[index].available)
      mask |= 1U << index;
  return mask;
}

constexpr std::uint32_t defaultPermissions() {
  std::uint32_t mask = 0;
  for (std::size_t index = 0; index < std::size(kTools); ++index)
    if (kTools[index].available && !kTools[index].destructive)
      mask |= 1U << index;
  return mask;
}

std::size_t toolIndex(const Tool* tool) {
  return tool == nullptr ? std::size(kTools) : static_cast<std::size_t>(tool - kTools);
}

bool toolEnabled(const Tool& tool) {
  const std::size_t index = toolIndex(&tool);
  taskENTER_CRITICAL(&permissions_lock);
  const bool enabled = index < 32 && (enabled_tools & (1U << index)) != 0;
  taskEXIT_CRITICAL(&permissions_lock);
  return enabled;
}

const Tool* findTool(const char* value) {
  if (value == nullptr)
    return nullptr;
  for (const auto& tool : kTools) {
    if (std::strcmp(value, tool.id) == 0 || std::strcmp(value, tool.name) == 0)
      return &tool;
  }
  return nullptr;
}

cJSON* descriptor(const Tool& tool) {
  cJSON* item = cJSON_CreateObject();
  cJSON_AddStringToObject(item, "id", tool.id);
  cJSON_AddStringToObject(item, "name", tool.name);
  cJSON_AddStringToObject(item, "title", tool.title);
  cJSON_AddStringToObject(item, "category", tool.category);
  cJSON_AddStringToObject(item, "description", tool.description);
  cJSON_AddBoolToObject(item, "enabled", toolEnabled(tool));
  cJSON_AddBoolToObject(item, "destructive", tool.destructive);
  cJSON_AddBoolToObject(item, "available", tool.available);
  cJSON* schema = cJSON_AddObjectToObject(item, "inputSchema");
  cJSON_AddStringToObject(schema, "type", "object");
  cJSON* properties = cJSON_AddObjectToObject(schema, "properties");
  if (std::strcmp(tool.name, "GetMatterOperation") == 0) {
    cJSON* operation = cJSON_AddObjectToObject(properties, "operation_id");
    cJSON_AddStringToObject(operation, "type", "integer");
    cJSON_AddNumberToObject(operation, "minimum", 1);
    cJSON* required = cJSON_AddArrayToObject(schema, "required");
    cJSON_AddItemToArray(required, cJSON_CreateString("operation_id"));
  } else if (std::strcmp(tool.name, "RemoveMatterDevice") == 0) {
    cJSON* device = cJSON_AddObjectToObject(properties, "device_id");
    cJSON_AddStringToObject(device, "type", "string");
    cJSON* confirmation = cJSON_AddObjectToObject(properties, "confirmation");
    cJSON_AddStringToObject(confirmation, "type", "string");
    cJSON* required = cJSON_AddArrayToObject(schema, "required");
    cJSON_AddItemToArray(required, cJSON_CreateString("device_id"));
    cJSON_AddItemToArray(required, cJSON_CreateString("confirmation"));
  } else if (std::strcmp(tool.name, "SetOnOff") == 0 || std::strcmp(tool.name, "SetLevel") == 0 ||
             std::strcmp(tool.name, "SetColorTemperature") == 0) {
    cJSON* device = cJSON_AddObjectToObject(properties, "device_id");
    cJSON_AddStringToObject(device, "type", "string");
    cJSON* endpoint = cJSON_AddObjectToObject(properties, "endpoint_id");
    cJSON_AddStringToObject(endpoint, "type", "integer");
    cJSON_AddNumberToObject(endpoint, "minimum", 1);
    cJSON* required = cJSON_AddArrayToObject(schema, "required");
    cJSON_AddItemToArray(required, cJSON_CreateString("device_id"));
    cJSON_AddItemToArray(required, cJSON_CreateString("endpoint_id"));
    if (std::strcmp(tool.name, "SetOnOff") == 0) {
      cJSON* on = cJSON_AddObjectToObject(properties, "on");
      cJSON_AddStringToObject(on, "type", "boolean");
      cJSON_AddItemToArray(required, cJSON_CreateString("on"));
    } else {
      const bool level = std::strcmp(tool.name, "SetLevel") == 0;
      cJSON* value = cJSON_AddObjectToObject(properties, level ? "level" : "mireds");
      cJSON_AddStringToObject(value, "type", "integer");
      cJSON_AddNumberToObject(value, "minimum", level ? 0 : 50);
      cJSON_AddNumberToObject(value, "maximum", level ? 254 : 1000);
      cJSON_AddItemToArray(required, cJSON_CreateString(level ? "level" : "mireds"));
      cJSON* transition = cJSON_AddObjectToObject(properties, "transition_time_ds");
      cJSON_AddStringToObject(transition, "type", "integer");
      cJSON_AddNumberToObject(transition, "minimum", 0);
      cJSON_AddNumberToObject(transition, "maximum", 6000);
    }
  }
  cJSON_AddBoolToObject(schema, "additionalProperties", false);
  return item;
}

cJSON* modelList(const HttpRuntime& runtime, core_domain::ModelKind kind, const char* key) {
  cJSON* result = cJSON_CreateObject();
  cJSON* items = cJSON_AddArrayToObject(result, key);
  const std::size_t count = runtime.models.count(kind);
  for (std::size_t index = 0; index < count; ++index) {
    core_domain::ModelUpsert model{};
    if (!runtime.models.get(kind, index, model))
      continue;
    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "id", model.id);
    cJSON_AddStringToObject(item, "name", model.name);
    if (kind == core_domain::ModelKind::automation)
      cJSON_AddBoolToObject(item, "enabled", model.enabled);
    cJSON_AddItemToArray(items, item);
  }
  return result;
}

bool parseEndpoint(const cJSON* arguments, std::uint64_t& node_id, std::uint16_t& endpoint_id) {
  const cJSON* device = cJSON_GetObjectItemCaseSensitive(arguments, "device_id");
  const cJSON* endpoint = cJSON_GetObjectItemCaseSensitive(arguments, "endpoint_id");
  if (!cJSON_IsString(device) || !cJSON_IsNumber(endpoint) || endpoint->valueint <= 0 ||
      endpoint->valueint > 0xffff)
    return false;
  errno = 0;
  char* end = nullptr;
  node_id = std::strtoull(device->valuestring, &end, 16);
  endpoint_id = static_cast<std::uint16_t>(endpoint->valueint);
  return node_id != 0 && errno == 0 && end != nullptr && *end == '\0';
}

cJSON* rpcError(const cJSON* id, int code, const char* message) {
  cJSON* response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "jsonrpc", "2.0");
  cJSON_AddItemToObject(response, "id",
                        id == nullptr ? cJSON_CreateNull() : cJSON_Duplicate(id, true));
  cJSON* error = cJSON_AddObjectToObject(response, "error");
  cJSON_AddNumberToObject(error, "code", code);
  cJSON_AddStringToObject(error, "message", message);
  return response;
}

} // namespace

esp_err_t McpService::initialize(const HttpRuntime& runtime) {
  const core_security::RecordSpec spec{kPermissionsPath,
                                       sizeof(kPermissionsPath) - 1,
                                       core_security::Namespace::Ai,
                                       core_security::RecordType::AiToolPermissions,
                                       kPermissionsSchema,
                                       0};
  std::uint8_t plaintext[8]{};
  std::uint8_t encrypted[256]{};
  std::size_t size = 0;
  core_security::Metadata metadata{};
  const auto status = runtime.storage.get(spec, encrypted, sizeof(encrypted), plaintext,
                                          sizeof(plaintext), size, metadata);
  std::uint32_t mask = defaultPermissions();
  std::uint64_t generation = 0;
  if (status == core_security::Status::Ok) {
    if (size != sizeof(plaintext) || std::memcmp(plaintext, "MCP1", 4) != 0) {
      mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
      return ESP_ERR_INVALID_RESPONSE;
    }
    mask = static_cast<std::uint32_t>(plaintext[4]) |
           (static_cast<std::uint32_t>(plaintext[5]) << 8U) |
           (static_cast<std::uint32_t>(plaintext[6]) << 16U) |
           (static_cast<std::uint32_t>(plaintext[7]) << 24U);
    generation = metadata.generation;
  } else if (status != core_security::Status::NotFound) {
    mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
    return ESP_ERR_INVALID_RESPONSE;
  }
  mask &= availablePermissions();
  taskENTER_CRITICAL(&permissions_lock);
  enabled_tools = mask;
  permissions_generation = generation;
  taskEXIT_CRITICAL(&permissions_lock);
  mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
  return ESP_OK;
}

esp_err_t McpService::setPermission(const HttpRuntime& runtime, const char* tool, bool enabled) {
  const Tool* selected_tool = findTool(tool);
  const std::size_t index = toolIndex(selected_tool);
  if (selected_tool == nullptr || index >= 32 || !selected_tool->available)
    return ESP_ERR_NOT_SUPPORTED;
  taskENTER_CRITICAL(&permissions_lock);
  std::uint32_t mask = enabled_tools;
  const std::uint64_t generation = permissions_generation + 1;
  taskEXIT_CRITICAL(&permissions_lock);
  mask = enabled ? mask | (1U << index) : mask & ~(1U << index);
  std::uint8_t plaintext[8]{'M',
                            'C',
                            'P',
                            '1',
                            static_cast<std::uint8_t>(mask),
                            static_cast<std::uint8_t>(mask >> 8U),
                            static_cast<std::uint8_t>(mask >> 16U),
                            static_cast<std::uint8_t>(mask >> 24U)};
  std::uint8_t encrypted[256]{};
  const core_security::RecordSpec spec{kPermissionsPath,
                                       sizeof(kPermissionsPath) - 1,
                                       core_security::Namespace::Ai,
                                       core_security::RecordType::AiToolPermissions,
                                       kPermissionsSchema,
                                       generation};
  const auto status = runtime.storage.put(spec, plaintext, sizeof(plaintext), runtime.random,
                                          encrypted, sizeof(encrypted));
  mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
  if (status != core_security::Status::Ok) {
    switch (status) {
    case core_security::Status::BufferTooSmall:
    case core_security::Status::TooLarge:
      return ESP_ERR_NO_MEM;
    case core_security::Status::Invalid:
    case core_security::Status::UnsupportedFormat:
      return ESP_ERR_INVALID_ARG;
    case core_security::Status::IoError:
      return ESP_ERR_INVALID_STATE;
    default:
      return ESP_FAIL;
    }
  }
  taskENTER_CRITICAL(&permissions_lock);
  enabled_tools = mask;
  permissions_generation = generation;
  taskEXIT_CRITICAL(&permissions_lock);
  return ESP_OK;
}

cJSON* McpService::tools() {
  cJSON* response = cJSON_CreateObject();
  cJSON* items = cJSON_AddArrayToObject(response, "tools");
  for (const auto& tool : kTools)
    cJSON_AddItemToArray(items, descriptor(tool));
  return response;
}

cJSON* McpService::context(const HttpRuntime& runtime) {
  cJSON* response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "system_prompt",
                          "Operate Core Pilot only through enabled local tools. Never assume a "
                          "command succeeded until its operation completes.");
  const auto devices = runtime.devices.snapshot();
  const auto matter = runtime.matter.status();
  char context[192]{};
  std::snprintf(
      context, sizeof(context),
      "{\"product\":\"Core Pilot\",\"devices\":%u,\"matter_ready\":%s,\"border_router_ready\":%s}",
      static_cast<unsigned>(devices.count), matter.commissioner_ready ? "true" : "false",
      matter.border_router_ready ? "true" : "false");
  cJSON_AddStringToObject(response, "context_json", context);
  cJSON* registry = tools();
  cJSON_AddItemToObject(response, "tools", cJSON_DetachItemFromObject(registry, "tools"));
  cJSON_Delete(registry);
  return response;
}

esp_err_t McpService::invoke(const HttpRuntime& runtime, const char* tool_name,
                             const cJSON* arguments, cJSON*& result) {
  result = nullptr;
  const Tool* tool = findTool(tool_name);
  if (tool == nullptr || !tool->available || arguments == nullptr || !cJSON_IsObject(arguments))
    return ESP_ERR_NOT_SUPPORTED;
  if (!toolEnabled(*tool))
    return ESP_ERR_NOT_SUPPORTED;

  if (std::strcmp(tool->name, "GetHubStatus") == 0) {
    const auto matter = runtime.matter.status();
    result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "commissioner_ready", matter.commissioner_ready);
    cJSON_AddBoolToObject(result, "border_router_ready", matter.border_router_ready);
    cJSON_AddNumberToObject(result, "devices", runtime.devices.snapshot().count);
    return ESP_OK;
  }
  if (std::strcmp(tool->name, "GetMatterOperation") == 0) {
    const cJSON* requested = cJSON_GetObjectItemCaseSensitive(arguments, "operation_id");
    if (!cJSON_IsNumber(requested) || requested->valuedouble != requested->valueint ||
        requested->valueint <= 0)
      return ESP_ERR_INVALID_ARG;
    const auto operation = runtime.matter.commandStatus();
    if (operation.operation_id != static_cast<std::uint32_t>(requested->valuedouble))
      return ESP_ERR_NOT_FOUND;
    result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "operation_id", operation.operation_id);
    const char* phase = "idle";
    switch (operation.state) {
    case CoreMatter::CommandState::idle:
      break;
    case CoreMatter::CommandState::queued:
      phase = "queued";
      break;
    case CoreMatter::CommandState::invoking:
      phase = "invoking";
      break;
    case CoreMatter::CommandState::complete:
      phase = "complete";
      break;
    case CoreMatter::CommandState::failed:
      phase = "failed";
      break;
    }
    cJSON_AddStringToObject(result, "phase", phase);
    cJSON_AddStringToObject(result, "last_error", esp_err_to_name(operation.last_error));
    return ESP_OK;
  }
  if (std::strcmp(tool->name, "RemoveMatterDevice") == 0) {
    const cJSON* device = cJSON_GetObjectItemCaseSensitive(arguments, "device_id");
    const cJSON* confirmation = cJSON_GetObjectItemCaseSensitive(arguments, "confirmation");
    if (!cJSON_IsString(device) || !cJSON_IsString(confirmation) ||
        std::strcmp(confirmation->valuestring, "CONFIRM") != 0)
      return ESP_ERR_INVALID_ARG;
    errno = 0;
    char* end = nullptr;
    const std::uint64_t node_id = std::strtoull(device->valuestring, &end, 16);
    bool registered = false;
    const auto devices = runtime.devices.snapshot();
    for (std::size_t index = 0; index < devices.count; ++index)
      registered = registered || devices.entries[index].node_id == node_id;
    if (node_id == 0 || errno != 0 || end == nullptr || *end != '\0' || !registered)
      return ESP_ERR_INVALID_ARG;
    std::uint32_t operation_id = 0;
    const esp_err_t queued = runtime.matter.unpairDevice(node_id, operation_id);
    if (queued != ESP_OK)
      return queued;
    result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "queued");
    cJSON_AddNumberToObject(result, "operation_id", operation_id);
    return ESP_OK;
  }
  if (std::strcmp(tool->name, "ListDevices") == 0) {
    result = cJSON_CreateObject();
    cJSON* devices = cJSON_AddArrayToObject(result, "devices");
    const auto snapshot = runtime.devices.snapshot();
    for (std::size_t index = 0; index < snapshot.count; ++index) {
      const auto& endpoint = snapshot.entries[index];
      cJSON* item = cJSON_CreateObject();
      char node[17]{};
      std::snprintf(node, sizeof(node), "%016llx",
                    static_cast<unsigned long long>(endpoint.node_id));
      cJSON_AddStringToObject(item, "device_id", node);
      cJSON_AddNumberToObject(item, "endpoint_id", endpoint.endpoint_id);
      cJSON_AddNumberToObject(item, "capabilities", endpoint.capabilities);
      cJSON_AddBoolToObject(item, "reachable", endpoint.reachable);
      cJSON_AddItemToArray(devices, item);
    }
    return ESP_OK;
  }
  if (std::strcmp(tool->name, "ListScenes") == 0)
    result = modelList(runtime, core_domain::ModelKind::scene, "scenes");
  else if (std::strcmp(tool->name, "ListAutomations") == 0)
    result = modelList(runtime, core_domain::ModelKind::automation, "automations");
  else if (std::strcmp(tool->name, "SetOnOff") == 0) {
    std::uint64_t node_id = 0;
    std::uint16_t endpoint_id = 0;
    const cJSON* on = cJSON_GetObjectItemCaseSensitive(arguments, "on");
    if (!parseEndpoint(arguments, node_id, endpoint_id) || !cJSON_IsBool(on) ||
        !runtime.devices.contains(node_id, endpoint_id))
      return ESP_ERR_INVALID_ARG;
    std::uint32_t operation_id = 0;
    const esp_err_t queued =
        runtime.matter.setOnOff(node_id, endpoint_id, cJSON_IsTrue(on), operation_id);
    if (queued != ESP_OK)
      return queued;
    result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "queued");
    cJSON_AddNumberToObject(result, "operation_id", operation_id);
  } else if (std::strcmp(tool->name, "SetLevel") == 0 ||
             std::strcmp(tool->name, "SetColorTemperature") == 0) {
    std::uint64_t node_id = 0;
    std::uint16_t endpoint_id = 0;
    const bool level_command = std::strcmp(tool->name, "SetLevel") == 0;
    const cJSON* value =
        cJSON_GetObjectItemCaseSensitive(arguments, level_command ? "level" : "mireds");
    const cJSON* transition = cJSON_GetObjectItemCaseSensitive(arguments, "transition_time_ds");
    const bool transition_valid =
        transition == nullptr ||
        (cJSON_IsNumber(transition) && transition->valuedouble == transition->valueint &&
         transition->valueint >= 0 && transition->valueint <= 6000);
    if (!parseEndpoint(arguments, node_id, endpoint_id) || !cJSON_IsNumber(value) ||
        value->valuedouble != value->valueint || value->valueint < (level_command ? 0 : 50) ||
        value->valueint > (level_command ? 254 : 1000) || !transition_valid ||
        !runtime.devices.contains(node_id, endpoint_id))
      return ESP_ERR_INVALID_ARG;
    const auto transition_time =
        static_cast<std::uint16_t>(transition == nullptr ? 0 : transition->valueint);
    std::uint32_t operation_id = 0;
    const esp_err_t queued =
        level_command
            ? runtime.matter.setLevel(node_id, endpoint_id,
                                      static_cast<std::uint8_t>(value->valueint), transition_time,
                                      operation_id)
            : runtime.matter.setColorTemperature(node_id, endpoint_id,
                                                 static_cast<std::uint16_t>(value->valueint),
                                                 transition_time, operation_id);
    if (queued != ESP_OK)
      return queued;
    result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "queued");
    cJSON_AddNumberToObject(result, "operation_id", operation_id);
  }
  return result == nullptr ? ESP_ERR_NOT_SUPPORTED : ESP_OK;
}

cJSON* McpService::jsonRpc(const HttpRuntime& runtime, const cJSON* request, bool& has_response) {
  has_response = true;
  const cJSON* id =
      cJSON_IsObject(request) ? cJSON_GetObjectItemCaseSensitive(request, "id") : nullptr;
  const bool notification = cJSON_IsObject(request) && id == nullptr;
  const cJSON* version =
      cJSON_IsObject(request) ? cJSON_GetObjectItemCaseSensitive(request, "jsonrpc") : nullptr;
  const cJSON* method =
      cJSON_IsObject(request) ? cJSON_GetObjectItemCaseSensitive(request, "method") : nullptr;
  const bool valid_id =
      id == nullptr || cJSON_IsString(id) || cJSON_IsNumber(id) || cJSON_IsNull(id);
  if (!cJSON_IsObject(request) || !valid_id || !cJSON_IsString(version) ||
      std::strcmp(version->valuestring, "2.0") != 0 || !cJSON_IsString(method)) {
    if (notification) {
      has_response = false;
      return nullptr;
    }
    return rpcError(id, -32600, "Invalid Request");
  }

  if (notification && std::strcmp(method->valuestring, "notifications/initialized") == 0) {
    has_response = false;
    return nullptr;
  }

  cJSON* result = nullptr;
  if (std::strcmp(method->valuestring, "initialize") == 0) {
    const cJSON* params = cJSON_GetObjectItemCaseSensitive(request, "params");
    const cJSON* protocol = cJSON_GetObjectItemCaseSensitive(params, "protocolVersion");
    const cJSON* capabilities = cJSON_GetObjectItemCaseSensitive(params, "capabilities");
    const cJSON* client = cJSON_GetObjectItemCaseSensitive(params, "clientInfo");
    const cJSON* client_name = cJSON_GetObjectItemCaseSensitive(client, "name");
    const cJSON* client_version = cJSON_GetObjectItemCaseSensitive(client, "version");
    if (!cJSON_IsObject(params) || !cJSON_IsString(protocol) || !cJSON_IsObject(capabilities) ||
        !cJSON_IsObject(client) || !cJSON_IsString(client_name) ||
        !cJSON_IsString(client_version)) {
      if (notification) {
        has_response = false;
        return nullptr;
      }
      return rpcError(id, -32602, "Invalid params");
    }
    result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "protocolVersion", "2025-06-18");
    cJSON* server_capabilities = cJSON_AddObjectToObject(result, "capabilities");
    cJSON_AddObjectToObject(server_capabilities, "tools");
    cJSON* server = cJSON_AddObjectToObject(result, "serverInfo");
    cJSON_AddStringToObject(server, "name", "core-pilot");
    cJSON_AddStringToObject(server, "version", "0.1.0");
  } else if (std::strcmp(method->valuestring, "tools/list") == 0) {
    result = cJSON_CreateObject();
    cJSON* items = cJSON_AddArrayToObject(result, "tools");
    for (const auto& tool : kTools) {
      if (tool.available && toolEnabled(tool))
        cJSON_AddItemToArray(items, descriptor(tool));
    }
  } else if (std::strcmp(method->valuestring, "tools/call") == 0) {
    const cJSON* params = cJSON_GetObjectItemCaseSensitive(request, "params");
    const cJSON* name = cJSON_GetObjectItemCaseSensitive(params, "name");
    const cJSON* arguments = cJSON_GetObjectItemCaseSensitive(params, "arguments");
    if (!cJSON_IsObject(params) || !cJSON_IsString(name) ||
        (arguments != nullptr && !cJSON_IsObject(arguments))) {
      if (notification) {
        has_response = false;
        return nullptr;
      }
      return rpcError(id, -32602, "Invalid params");
    }
    cJSON* empty_arguments = arguments == nullptr ? cJSON_CreateObject() : nullptr;
    cJSON* invocation = nullptr;
    const esp_err_t status = invoke(runtime, name->valuestring,
                                    arguments == nullptr ? empty_arguments : arguments, invocation);
    cJSON_Delete(empty_arguments);
    result = cJSON_CreateObject();
    cJSON* content = cJSON_AddArrayToObject(result, "content");
    cJSON* entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "type", "text");
    if (status == ESP_OK) {
      char* text = cJSON_PrintUnformatted(invocation);
      cJSON_AddStringToObject(entry, "text", text == nullptr ? "{}" : text);
      cJSON_free(text);
    } else {
      cJSON_AddStringToObject(entry, "text", "Tool execution was rejected.");
      cJSON_AddBoolToObject(result, "isError", true);
    }
    cJSON_AddItemToArray(content, entry);
    cJSON_Delete(invocation);
  } else {
    if (notification) {
      has_response = false;
      return nullptr;
    }
    return rpcError(id, -32601, "Method not found");
  }

  if (notification) {
    cJSON_Delete(result);
    has_response = false;
    return nullptr;
  }
  cJSON* response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "jsonrpc", "2.0");
  cJSON_AddItemToObject(response, "id", cJSON_Duplicate(id, true));
  cJSON_AddItemToObject(response, "result", result);
  return response;
}

} // namespace core_pilot
