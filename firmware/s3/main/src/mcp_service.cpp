#include "mcp_service.hpp"

#include "automation_service.hpp"
#include "cJSON.h"
#include "core_domain.hpp"
#include "core_matter.hpp"
#include "core_security.hpp"
#include "device_registry.hpp"
#include "http_server.hpp"
#include "mbedtls/platform_util.h"
#include "mcp_permissions.hpp"
#include "model_repository.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string_view>

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
    {"rooms.list", "ListRooms", "List rooms", "Rooms", "List stored rooms.", true, false},
    {"rooms.create", "CreateRoom", "Create room", "Rooms", "Create a room.", true, false},
    {"rooms.update", "UpdateRoom", "Update room", "Rooms", "Update a room.", true, false},
    {"rooms.delete", "DeleteRoom", "Delete room", "Rooms",
     "Delete a room after exact per-call confirmation.", true, true},
    {"groups.list", "ListGroups", "List groups", "Groups", "List stored groups.", true, false},
    {"groups.create", "CreateGroup", "Create group", "Groups", "Create a group.", true, false},
    {"groups.update", "UpdateGroup", "Update group", "Groups", "Update a group.", true, false},
    {"groups.delete", "DeleteGroup", "Delete group", "Groups",
     "Delete a group after exact per-call confirmation.", true, true},
    {"scenes.activate", "ActivateScene", "Activate scene", "Scenes",
     "Run a stored scene asynchronously.", false, false},
    {"scenes.list", "ListScenes", "List scenes", "Scenes", "List stored scenes.", true, false},
    {"scenes.create", "CreateScene", "Create scene", "Scenes", "Create a scene.", true, false},
    {"scenes.update", "UpdateScene", "Update scene", "Scenes", "Update a scene.", true, false},
    {"scenes.delete", "DeleteScene", "Delete scene", "Scenes",
     "Delete a scene after exact per-call confirmation.", true, true},
    {"automations.list", "ListAutomations", "List automations", "Automations",
     "List stored automations.", true, false},
    {"automations.create", "CreateAutomation", "Create automation", "Automations",
     "Create a bounded local automation.", true, false},
    {"automations.update", "UpdateAutomation", "Update automation", "Automations",
     "Update a bounded local automation.", true, false},
    {"automations.delete", "DeleteAutomation", "Delete automation", "Automations",
     "Delete an automation after exact per-call confirmation.", true, true},
    {"automations.run", "RunAutomation", "Run automation", "Automations",
     "Queue an enabled manual automation through AutomationService.", true, false},
    {"automations.enable", "EnableAutomation", "Enable automation", "Automations",
     "Enable a stored automation.", true, false},
    {"automations.disable", "DisableAutomation", "Disable automation", "Automations",
     "Disable a stored automation.", true, false},
    {"hub.status", "GetHubStatus", "Get hub status", "Hub",
     "Read local network, Matter, and Thread readiness.", true, false},
    {"matter.get_operation", "GetMatterOperation", "Get Matter operation", "Matter",
     "Read the current or most recent Matter command result.", true, false},
    {"matter.remove_device", "RemoveMatterDevice", "Remove Matter device", "Matter",
     "Remove a commissioned node after exact per-call confirmation.", true, true},
};
static_assert(std::size(kTools) <= 32, "MCP permissions must fit in the persisted 32-bit mask");

portMUX_TYPE permissions_lock = portMUX_INITIALIZER_UNLOCKED;
StaticSemaphore_t permission_write_mutex_buffer{};
SemaphoreHandle_t permission_write_mutex = nullptr;
std::uint32_t enabled_tools = 0;
std::uint64_t permissions_generation = 0;

constexpr std::uint32_t availablePermissions() {
  std::uint32_t mask = 0;
  for (std::size_t index = 0; index < std::size(kTools); ++index)
    if (kTools[index].available)
      mask |= 1U << index;
  return mask;
}

constexpr bool readOnlyDefault(const Tool& tool) {
  const std::string_view id{tool.id};
  return id == "devices.list" || id.ends_with(".list") || id == "hub.status" ||
         id == "matter.get_operation";
}

constexpr std::uint32_t defaultPermissions() {
  std::uint32_t mask = 0;
  for (std::size_t index = 0; index < std::size(kTools); ++index)
    if (kTools[index].available && readOnlyDefault(kTools[index]))
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

void addRequired(cJSON* schema, const char* const* fields, std::size_t count) {
  cJSON* required = cJSON_AddArrayToObject(schema, "required");
  for (std::size_t index = 0; index < count; ++index)
    cJSON_AddItemToArray(required, cJSON_CreateString(fields[index]));
}

cJSON* addStringProperty(cJSON* properties, const char* name, std::size_t max_length) {
  cJSON* property = cJSON_AddObjectToObject(properties, name);
  cJSON_AddStringToObject(property, "type", "string");
  cJSON_AddNumberToObject(property, "minLength", 1);
  cJSON_AddNumberToObject(property, "maxLength", max_length);
  cJSON_AddStringToObject(property, "description",
                          "Limits are enforced as UTF-8 bytes by Core Pilot; maxLength documents "
                          "the ASCII-equivalent upper bound.");
  return property;
}

void addModelIdProperty(cJSON* properties) {
  cJSON* property = addStringProperty(properties, "id", core_domain::max_id);
  cJSON_AddStringToObject(property, "pattern", "^[A-Za-z0-9_-]+$");
  cJSON_AddStringToObject(
      property, "description",
      "Existing model ID, limited to 48 UTF-8 bytes; ASCII ID characters only.");
}

void addStringArrayProperty(cJSON* properties, const char* name, std::size_t max_items) {
  cJSON* property = cJSON_AddObjectToObject(properties, name);
  cJSON_AddStringToObject(property, "type", "array");
  cJSON_AddNumberToObject(property, "maxItems", max_items);
  cJSON* items = cJSON_AddObjectToObject(property, "items");
  cJSON_AddStringToObject(items, "type", "string");
  cJSON_AddNumberToObject(items, "maxLength", core_domain::max_detail);
}

bool named(const Tool& tool, const char* suffix) {
  return std::strcmp(tool.name, suffix) == 0;
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
  if (named(tool, "GetMatterOperation")) {
    cJSON* operation = cJSON_AddObjectToObject(properties, "operation_id");
    cJSON_AddStringToObject(operation, "type", "integer");
    cJSON_AddNumberToObject(operation, "minimum", 1);
    cJSON_AddNumberToObject(operation, "maximum", UINT32_MAX);
    const char* required[]{"operation_id"};
    addRequired(schema, required, std::size(required));
  } else if (named(tool, "RemoveMatterDevice")) {
    cJSON* device = addStringProperty(properties, "device_id", 16);
    cJSON_AddStringToObject(device, "pattern", "^[0-9A-Fa-f]{1,16}$");
    cJSON_AddStringToObject(device, "description",
                            "Nonzero hexadecimal Matter node ID without sign or 0x prefix.");
    cJSON* confirmation = addStringProperty(properties, "confirmation", 23);
    cJSON_AddStringToObject(confirmation, "description",
                            "Exact target-bound value: REMOVE <device_id>.");
    const char* required[]{"device_id", "confirmation"};
    addRequired(schema, required, std::size(required));
  } else if (named(tool, "SetOnOff") || named(tool, "SetLevel") ||
             named(tool, "SetColorTemperature")) {
    cJSON* device = addStringProperty(properties, "device_id", 16);
    cJSON_AddStringToObject(device, "pattern", "^[0-9A-Fa-f]{1,16}$");
    cJSON_AddStringToObject(device, "description",
                            "Nonzero hexadecimal Matter node ID without sign or 0x prefix.");
    cJSON* endpoint = cJSON_AddObjectToObject(properties, "endpoint_id");
    cJSON_AddStringToObject(endpoint, "type", "integer");
    cJSON_AddNumberToObject(endpoint, "minimum", 1);
    cJSON_AddNumberToObject(endpoint, "maximum", 65535);
    if (named(tool, "SetOnOff")) {
      cJSON* on = cJSON_AddObjectToObject(properties, "on");
      cJSON_AddStringToObject(on, "type", "boolean");
      const char* required[]{"device_id", "endpoint_id", "on"};
      addRequired(schema, required, std::size(required));
    } else {
      const bool level = named(tool, "SetLevel");
      cJSON* value = cJSON_AddObjectToObject(properties, level ? "level" : "mireds");
      cJSON_AddStringToObject(value, "type", "integer");
      cJSON_AddNumberToObject(value, "minimum", level ? 0 : 50);
      cJSON_AddNumberToObject(value, "maximum", level ? 254 : 1000);
      cJSON* transition = cJSON_AddObjectToObject(properties, "transition_time_ds");
      cJSON_AddStringToObject(transition, "type", "integer");
      cJSON_AddNumberToObject(transition, "minimum", 0);
      cJSON_AddNumberToObject(transition, "maximum", 6000);
      const char* required[]{"device_id", "endpoint_id", level ? "level" : "mireds"};
      addRequired(schema, required, std::size(required));
    }
  } else if (std::strstr(tool.id, ".create") || std::strstr(tool.id, ".update")) {
    addStringProperty(properties, "name", core_domain::max_name);
    const bool update = std::strstr(tool.id, ".update") != nullptr;
    const bool automation = std::strstr(tool.id, "automations.") == tool.id;
    const bool group = std::strstr(tool.id, "groups.") == tool.id;
    const bool room = std::strstr(tool.id, "rooms.") == tool.id;
    if (update)
      addModelIdProperty(properties);
    if (automation) {
      cJSON* enabled = cJSON_AddObjectToObject(properties, "enabled");
      cJSON_AddStringToObject(enabled, "type", "boolean");
      addStringProperty(properties, "trigger", core_domain::max_detail);
    }
    if (!room) {
      addStringArrayProperty(properties, group ? "device_ids" : "actions",
                             group ? core_domain::max_members : core_domain::max_actions);
      if (!group) {
        cJSON* actions = cJSON_GetObjectItemCaseSensitive(properties, "actions");
        cJSON_AddNumberToObject(actions, "minItems", 1);
      }
    }
    const char* required[5]{};
    std::size_t count = 0;
    if (update)
      required[count++] = "id";
    required[count++] = "name";
    if (automation) {
      required[count++] = "enabled";
      required[count++] = "trigger";
    }
    if (!room)
      required[count++] = group ? "device_ids" : "actions";
    addRequired(schema, required, count);
  } else if (std::strstr(tool.id, ".delete")) {
    addModelIdProperty(properties);
    cJSON* confirmation = addStringProperty(properties, "confirmation", core_domain::max_id + 7);
    cJSON_AddStringToObject(confirmation, "pattern", "^DELETE .+$");
    cJSON_AddStringToObject(confirmation, "description", "Exact target-bound value: DELETE <id>.");
    const char* required[]{"id", "confirmation"};
    addRequired(schema, required, std::size(required));
  } else if (named(tool, "RunAutomation") || named(tool, "EnableAutomation") ||
             named(tool, "DisableAutomation")) {
    addModelIdProperty(properties);
    const char* required[]{"id"};
    addRequired(schema, required, std::size(required));
  }
  cJSON_AddBoolToObject(schema, "additionalProperties", false);
  return item;
}

struct ModelListContext final {
  cJSON* items;
  core_domain::ModelKind kind;
};

bool appendModel(const core_domain::ModelUpsert& model, void* opaque) {
  auto& context = *static_cast<ModelListContext*>(opaque);
  cJSON* item = cJSON_CreateObject();
  cJSON_AddStringToObject(item, "id", model.id);
  cJSON_AddStringToObject(item, "name", model.name);
  if (context.kind == core_domain::ModelKind::automation) {
    cJSON_AddBoolToObject(item, "enabled", model.enabled);
    cJSON_AddStringToObject(item, "trigger", model.trigger);
  }
  if (context.kind != core_domain::ModelKind::room) {
    cJSON* details = cJSON_AddArrayToObject(
        item, context.kind == core_domain::ModelKind::group ? "device_ids" : "actions");
    for (std::size_t index = 0; details != nullptr && index < model.detail_count; ++index)
      cJSON_AddItemToArray(details, cJSON_CreateString(model.details[index]));
    if (details == nullptr || cJSON_GetArraySize(details) != static_cast<int>(model.detail_count)) {
      cJSON_Delete(item);
      return false;
    }
  }
  cJSON_AddItemToArray(context.items, item);
  return true;
}

cJSON* modelList(const HttpRuntime& runtime, core_domain::ModelKind kind, const char* key) {
  cJSON* result = cJSON_CreateObject();
  cJSON* items = cJSON_AddArrayToObject(result, key);
  ModelListContext context{items, kind};
  std::size_t visited = 0;
  if (items == nullptr || !runtime.models.visit(kind, appendModel, &context, visited)) {
    cJSON_Delete(result);
    return nullptr;
  }
  return result;
}

bool hasOnlyKeys(const cJSON* object, const char* const* allowed, std::size_t count) {
  for (const cJSON* child = object == nullptr ? nullptr : object->child; child != nullptr;
       child = child->next) {
    bool found = false;
    for (std::size_t index = 0; index < count; ++index)
      found = found || std::strcmp(child->string, allowed[index]) == 0;
    if (!found)
      return false;
  }
  return true;
}

bool parseNodeId(const char* value, std::uint64_t& node_id) {
  if (value == nullptr)
    return false;
  const std::size_t length = std::strlen(value);
  if (length == 0 || length > 16)
    return false;
  std::uint64_t parsed = 0;
  for (std::size_t index = 0; index < length; ++index) {
    const char digit = value[index];
    std::uint8_t nibble = 0;
    if (digit >= '0' && digit <= '9')
      nibble = static_cast<std::uint8_t>(digit - '0');
    else if (digit >= 'a' && digit <= 'f')
      nibble = static_cast<std::uint8_t>(digit - 'a' + 10);
    else if (digit >= 'A' && digit <= 'F')
      nibble = static_cast<std::uint8_t>(digit - 'A' + 10);
    else
      return false;
    parsed = (parsed << 4U) | nibble;
  }
  node_id = parsed;
  return node_id != 0;
}

bool parseEndpoint(const cJSON* arguments, std::uint64_t& node_id, std::uint16_t& endpoint_id) {
  const cJSON* device = cJSON_GetObjectItemCaseSensitive(arguments, "device_id");
  const cJSON* endpoint = cJSON_GetObjectItemCaseSensitive(arguments, "endpoint_id");
  if (!cJSON_IsString(device) || !cJSON_IsNumber(endpoint) ||
      endpoint->valuedouble != endpoint->valueint || endpoint->valueint <= 0 ||
      endpoint->valueint > 0xffff || !parseNodeId(device->valuestring, node_id))
    return false;
  endpoint_id = static_cast<std::uint16_t>(endpoint->valueint);
  return true;
}

bool modelTool(const Tool& tool, core_domain::ModelKind& kind, bool& create, bool& update,
               bool& remove) {
  const char* separator = std::strchr(tool.id, '.');
  if (separator == nullptr)
    return false;
  const std::size_t prefix = static_cast<std::size_t>(separator - tool.id);
  if (prefix == 5 && std::memcmp(tool.id, "rooms", 5) == 0)
    kind = core_domain::ModelKind::room;
  else if (prefix == 6 && std::memcmp(tool.id, "groups", 6) == 0)
    kind = core_domain::ModelKind::group;
  else if (prefix == 6 && std::memcmp(tool.id, "scenes", 6) == 0)
    kind = core_domain::ModelKind::scene;
  else if (prefix == 11 && std::memcmp(tool.id, "automations", 11) == 0)
    kind = core_domain::ModelKind::automation;
  else
    return false;
  create = std::strcmp(separator + 1, "create") == 0;
  update = std::strcmp(separator + 1, "update") == 0;
  remove = std::strcmp(separator + 1, "delete") == 0;
  return create || update || remove;
}

esp_err_t mutateModel(const HttpRuntime& runtime, const Tool& tool, const cJSON* arguments,
                      cJSON*& result) {
  core_domain::ModelKind kind{};
  bool create = false;
  bool update = false;
  bool remove = false;
  if (!modelTool(tool, kind, create, update, remove))
    return ESP_ERR_NOT_SUPPORTED;
  core_domain::ModelUpsert model{};
  if (remove) {
    const char* allowed[]{"id", "confirmation"};
    char id[core_domain::max_id + 1]{};
    const cJSON* confirmation = cJSON_GetObjectItemCaseSensitive(arguments, "confirmation");
    if (!hasOnlyKeys(arguments, allowed, std::size(allowed)) ||
        core_domain::parse_model_delete(arguments, id) != core_domain::ModelError::ok ||
        !cJSON_IsString(confirmation))
      return ESP_ERR_INVALID_ARG;
    char expected[core_domain::max_id + 8]{};
    std::snprintf(expected, sizeof(expected), "DELETE %s", id);
    if (std::strcmp(confirmation->valuestring, expected) != 0)
      return ESP_ERR_INVALID_ARG;
    model.has_id = true;
    std::memcpy(model.id, id, sizeof(id));
  } else {
    const bool room = kind == core_domain::ModelKind::room;
    const bool group = kind == core_domain::ModelKind::group;
    const bool automation = kind == core_domain::ModelKind::automation;
    const char* allowed[5]{"id", "name", group ? "device_ids" : "actions", "enabled", "trigger"};
    const std::size_t allowed_count = room ? 2 : automation ? 5 : 3;
    if (!hasOnlyKeys(arguments, allowed, allowed_count) ||
        core_domain::parse_model_upsert(arguments, kind, model) != core_domain::ModelError::ok ||
        (create && model.has_id) || (update && !model.has_id))
      return ESP_ERR_INVALID_ARG;
  }
  const auto operation = remove   ? ModelRepository::Mutation::remove
                         : create ? ModelRepository::Mutation::create
                                  : ModelRepository::Mutation::update;
  ModelRepository::Result mutation{};
  if (!runtime.models.submit(operation, kind, model, mutation))
    return ESP_ERR_TIMEOUT;
  if (mutation.status != core_security::Status::Ok)
    return mutation.status == core_security::Status::NotFound ? ESP_ERR_NOT_FOUND : ESP_FAIL;
  result = cJSON_CreateObject();
  cJSON_AddStringToObject(result, "status", remove ? "deleted" : create ? "created" : "updated");
  cJSON_AddStringToObject(result, "id", mutation.id);
  return ESP_OK;
}

esp_err_t setAutomationEnabled(const HttpRuntime& runtime, const cJSON* arguments, bool enabled,
                               cJSON*& result) {
  const char* allowed[]{"id"};
  char id[core_domain::max_id + 1]{};
  if (!hasOnlyKeys(arguments, allowed, std::size(allowed)) ||
      core_domain::parse_model_delete(arguments, id) != core_domain::ModelError::ok)
    return ESP_ERR_INVALID_ARG;
  core_domain::ModelUpsert command{};
  command.has_id = true;
  command.enabled = enabled;
  std::memcpy(command.id, id, sizeof(command.id));
  ModelRepository::Result mutation{};
  if (!runtime.models.submit(ModelRepository::Mutation::set_automation_enabled,
                             core_domain::ModelKind::automation, command, mutation))
    return ESP_ERR_TIMEOUT;
  if (mutation.status != core_security::Status::Ok)
    return ESP_FAIL;
  result = cJSON_CreateObject();
  cJSON_AddStringToObject(result, "status", enabled ? "enabled" : "disabled");
  cJSON_AddStringToObject(result, "id", mutation.id);
  return ESP_OK;
}

esp_err_t runAutomation(const HttpRuntime& runtime, const cJSON* arguments, cJSON*& result) {
  const char* allowed[]{"id"};
  char id[core_domain::max_id + 1]{};
  if (!hasOnlyKeys(arguments, allowed, std::size(allowed)) ||
      core_domain::parse_model_delete(arguments, id) != core_domain::ModelError::ok)
    return ESP_ERR_INVALID_ARG;
  std::uint32_t operation_id = 0;
  const auto submitted = runtime.automations.submit(id, operation_id);
  result = cJSON_CreateObject();
  cJSON_AddStringToObject(result, "id", id);
  switch (submitted) {
  case AutomationService::SubmitResult::queued:
    cJSON_AddStringToObject(result, "status", "queued");
    cJSON_AddStringToObject(result, "detail", "Manual automation queued.");
    cJSON_AddNumberToObject(result, "operation_id", operation_id);
    return ESP_OK;
  case AutomationService::SubmitResult::unavailable:
    cJSON_AddStringToObject(result, "status", "AUTOMATION_UNAVAILABLE");
    cJSON_AddStringToObject(result, "detail", "The automation runtime is unavailable.");
    return ESP_ERR_INVALID_STATE;
  case AutomationService::SubmitResult::not_found:
    cJSON_AddStringToObject(result, "status", "AUTOMATION_NOT_FOUND");
    cJSON_AddStringToObject(result, "detail", "The automation does not exist.");
    return ESP_ERR_NOT_FOUND;
  case AutomationService::SubmitResult::disabled:
    cJSON_AddStringToObject(result, "status", "AUTOMATION_DISABLED");
    cJSON_AddStringToObject(result, "detail", "Enable the automation before running it.");
    return ESP_ERR_INVALID_STATE;
  case AutomationService::SubmitResult::not_manual:
    cJSON_AddStringToObject(result, "status", "AUTOMATION_NOT_MANUAL");
    cJSON_AddStringToObject(result, "detail", "Only manual automations can run on demand.");
    return ESP_ERR_INVALID_STATE;
  case AutomationService::SubmitResult::unsupported_action:
    cJSON_AddStringToObject(result, "status", "AUTOMATION_ACTION_UNSUPPORTED");
    cJSON_AddStringToObject(result, "detail", "The automation contains an unsupported action.");
    return ESP_ERR_NOT_SUPPORTED;
  case AutomationService::SubmitResult::target_unavailable:
    cJSON_AddStringToObject(result, "status", "AUTOMATION_TARGET_UNAVAILABLE");
    cJSON_AddStringToObject(result, "detail", "An automation target is unavailable.");
    return ESP_ERR_NOT_FOUND;
  case AutomationService::SubmitResult::scene_not_found:
    cJSON_AddStringToObject(result, "status", "AUTOMATION_SCENE_NOT_FOUND");
    cJSON_AddStringToObject(result, "detail", "A referenced scene does not exist.");
    return ESP_ERR_NOT_FOUND;
  case AutomationService::SubmitResult::scene_cycle:
    cJSON_AddStringToObject(result, "status", "AUTOMATION_SCENE_CYCLE");
    cJSON_AddStringToObject(result, "detail", "The automation contains a scene cycle.");
    return ESP_ERR_INVALID_STATE;
  case AutomationService::SubmitResult::scene_depth_exceeded:
  case AutomationService::SubmitResult::too_many_actions:
    cJSON_AddStringToObject(result, "status", "AUTOMATION_EXPANSION_TOO_LARGE");
    cJSON_AddStringToObject(result, "detail", "The expanded automation exceeds the limit.");
    return ESP_ERR_INVALID_SIZE;
  case AutomationService::SubmitResult::queue_full:
    cJSON_AddStringToObject(result, "status", "AUTOMATION_QUEUE_FULL");
    cJSON_AddStringToObject(result, "detail", "The automation queue is full.");
    return ESP_ERR_TIMEOUT;
  }
  return ESP_FAIL;
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
  if (permission_write_mutex == nullptr)
    permission_write_mutex = xSemaphoreCreateMutexStatic(&permission_write_mutex_buffer);
  if (permission_write_mutex == nullptr)
    return ESP_ERR_NO_MEM;
  const core_security::RecordSpec spec{
      mcp_permissions::v2_path.data(), mcp_permissions::v2_path.size(),
      core_security::Namespace::Ai,    core_security::RecordType::AiToolPermissions,
      mcp_permissions::v2_schema,      0};
  std::uint8_t plaintext[8]{};
  std::uint8_t encrypted[256]{};
  std::size_t size = 0;
  core_security::Metadata metadata{};
  auto status = runtime.storage.get(spec, encrypted, sizeof(encrypted), plaintext,
                                    sizeof(plaintext), size, metadata);
  std::uint32_t mask = defaultPermissions();
  std::uint64_t generation = 0;
  if (status == core_security::Status::Ok) {
    if (size != sizeof(plaintext) || std::memcmp(plaintext, "MCP2", 4) != 0) {
      mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
      return ESP_ERR_INVALID_RESPONSE;
    }
    mask = static_cast<std::uint32_t>(plaintext[4]) |
           (static_cast<std::uint32_t>(plaintext[5]) << 8U) |
           (static_cast<std::uint32_t>(plaintext[6]) << 16U) |
           (static_cast<std::uint32_t>(plaintext[7]) << 24U);
    generation = metadata.generation;
  } else if (status == core_security::Status::NotFound) {
    const core_security::RecordSpec old_spec{mcp_permissions::v1_path.data(),
                                             mcp_permissions::v1_path.size(),
                                             core_security::Namespace::Ai,
                                             core_security::RecordType::AiToolPermissions,
                                             1,
                                             0};
    status = runtime.storage.get(old_spec, encrypted, sizeof(encrypted), plaintext,
                                 sizeof(plaintext), size, metadata);
    if (status == core_security::Status::Ok) {
      if (size != sizeof(plaintext) || std::memcmp(plaintext, "MCP1", 4) != 0) {
        mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
        return ESP_ERR_INVALID_RESPONSE;
      }
      const std::uint32_t old_mask = static_cast<std::uint32_t>(plaintext[4]) |
                                     (static_cast<std::uint32_t>(plaintext[5]) << 8U) |
                                     (static_cast<std::uint32_t>(plaintext[6]) << 16U) |
                                     (static_cast<std::uint32_t>(plaintext[7]) << 24U);
      mask = 0;
      for (std::size_t index = 0; index < std::size(kTools); ++index)
        if (kTools[index].available && mcp_permissions::v1_enabled(old_mask, kTools[index].id))
          mask |= 1U << index;
      generation = 1;
      plaintext[0] = 'M';
      plaintext[1] = 'C';
      plaintext[2] = 'P';
      plaintext[3] = '2';
      plaintext[4] = static_cast<std::uint8_t>(mask);
      plaintext[5] = static_cast<std::uint8_t>(mask >> 8U);
      plaintext[6] = static_cast<std::uint8_t>(mask >> 16U);
      plaintext[7] = static_cast<std::uint8_t>(mask >> 24U);
      const core_security::RecordSpec migrated_spec{
          mcp_permissions::v2_path.data(), mcp_permissions::v2_path.size(),
          core_security::Namespace::Ai,    core_security::RecordType::AiToolPermissions,
          mcp_permissions::v2_schema,      generation};
      status = runtime.storage.put(migrated_spec, plaintext, sizeof(plaintext), runtime.random,
                                   encrypted, sizeof(encrypted));
      if (status != core_security::Status::Ok) {
        mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
        return ESP_ERR_INVALID_STATE;
      }
    } else if (status != core_security::Status::NotFound) {
      mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
      return ESP_ERR_INVALID_RESPONSE;
    }
  } else {
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

esp_err_t McpService::setPermission(const HttpRuntime& runtime, const char* tool, bool enabled,
                                    const char* confirmation) {
  const Tool* selected_tool = findTool(tool);
  const std::size_t index = toolIndex(selected_tool);
  if (selected_tool == nullptr || index >= 32 || !selected_tool->available)
    return ESP_ERR_NOT_SUPPORTED;
  if (enabled && selected_tool->destructive) {
    char expected[96]{};
    const int written = std::snprintf(expected, sizeof(expected), "ENABLE DESTRUCTIVE TOOL %s",
                                      selected_tool->name);
    const bool confirmed = written > 0 && static_cast<std::size_t>(written) < sizeof(expected) &&
                           confirmation != nullptr && std::strcmp(confirmation, expected) == 0;
    mbedtls_platform_zeroize(expected, sizeof(expected));
    if (!confirmed)
      return ESP_ERR_INVALID_ARG;
  }
  if (permission_write_mutex == nullptr ||
      xSemaphoreTake(permission_write_mutex, pdMS_TO_TICKS(5000)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  taskENTER_CRITICAL(&permissions_lock);
  std::uint32_t mask = enabled_tools;
  const std::uint64_t generation = permissions_generation + 1;
  taskEXIT_CRITICAL(&permissions_lock);
  mask = enabled ? mask | (1U << index) : mask & ~(1U << index);
  std::uint8_t plaintext[8]{'M',
                            'C',
                            'P',
                            '2',
                            static_cast<std::uint8_t>(mask),
                            static_cast<std::uint8_t>(mask >> 8U),
                            static_cast<std::uint8_t>(mask >> 16U),
                            static_cast<std::uint8_t>(mask >> 24U)};
  std::uint8_t encrypted[256]{};
  const core_security::RecordSpec spec{
      mcp_permissions::v2_path.data(), mcp_permissions::v2_path.size(),
      core_security::Namespace::Ai,    core_security::RecordType::AiToolPermissions,
      mcp_permissions::v2_schema,      generation};
  const auto status = runtime.storage.put(spec, plaintext, sizeof(plaintext), runtime.random,
                                          encrypted, sizeof(encrypted));
  mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
  esp_err_t result = ESP_OK;
  if (status != core_security::Status::Ok) {
    switch (status) {
    case core_security::Status::BufferTooSmall:
    case core_security::Status::TooLarge:
      result = ESP_ERR_NO_MEM;
      break;
    case core_security::Status::Invalid:
    case core_security::Status::UnsupportedFormat:
      result = ESP_ERR_INVALID_ARG;
      break;
    case core_security::Status::IoError:
      result = ESP_ERR_INVALID_STATE;
      break;
    default:
      result = ESP_FAIL;
      break;
    }
  } else {
    taskENTER_CRITICAL(&permissions_lock);
    enabled_tools = mask;
    permissions_generation = generation;
    taskEXIT_CRITICAL(&permissions_lock);
  }
  xSemaphoreGive(permission_write_mutex);
  return result;
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

esp_err_t McpService::preflight(const char* tool_name, bool& mutating, bool& destructive) {
  mutating = false;
  destructive = false;
  const Tool* tool = findTool(tool_name);
  if (tool == nullptr || !tool->available || !toolEnabled(*tool))
    return ESP_ERR_NOT_SUPPORTED;
  mutating = !readOnlyDefault(*tool);
  destructive = tool->destructive;
  return ESP_OK;
}

esp_err_t McpService::invoke(const HttpRuntime& runtime, const char* tool_name,
                             const cJSON* arguments, cJSON*& result) {
  result = nullptr;
  const Tool* tool = findTool(tool_name);
  if (tool == nullptr || !tool->available || arguments == nullptr || !cJSON_IsObject(arguments) ||
      !toolEnabled(*tool))
    return ESP_ERR_NOT_SUPPORTED;

  if (std::strcmp(tool->name, "GetHubStatus") == 0) {
    if (arguments->child != nullptr)
      return ESP_ERR_INVALID_ARG;
    const auto matter = runtime.matter.status();
    result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "commissioner_ready", matter.commissioner_ready);
    cJSON_AddBoolToObject(result, "border_router_ready", matter.border_router_ready);
    cJSON_AddNumberToObject(result, "devices", runtime.devices.snapshot().count);
    return ESP_OK;
  }
  if (std::strcmp(tool->name, "GetMatterOperation") == 0) {
    const char* allowed[]{"operation_id"};
    const cJSON* requested = cJSON_GetObjectItemCaseSensitive(arguments, "operation_id");
    if (!hasOnlyKeys(arguments, allowed, std::size(allowed)))
      return ESP_ERR_INVALID_ARG;
    if (!cJSON_IsNumber(requested) || requested->valuedouble != requested->valueint ||
        requested->valuedouble <= 0 || requested->valuedouble > UINT32_MAX)
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
    const char* allowed[]{"device_id", "confirmation"};
    std::uint64_t node_id = 0;
    char expected[24]{};
    if (!hasOnlyKeys(arguments, allowed, std::size(allowed)) || !cJSON_IsString(device) ||
        !cJSON_IsString(confirmation) || !parseNodeId(device->valuestring, node_id))
      return ESP_ERR_INVALID_ARG;
    std::snprintf(expected, sizeof(expected), "REMOVE %s", device->valuestring);
    if (std::strcmp(confirmation->valuestring, expected) != 0)
      return ESP_ERR_INVALID_ARG;
    bool registered = false;
    const auto devices = runtime.devices.snapshot();
    for (std::size_t index = 0; index < devices.count; ++index)
      registered = registered || devices.entries[index].node_id == node_id;
    if (!registered)
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
    if (arguments->child != nullptr)
      return ESP_ERR_INVALID_ARG;
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
  if (std::strcmp(tool->name, "ListRooms") == 0 || std::strcmp(tool->name, "ListGroups") == 0 ||
      std::strcmp(tool->name, "ListScenes") == 0 ||
      std::strcmp(tool->name, "ListAutomations") == 0) {
    if (arguments->child != nullptr)
      return ESP_ERR_INVALID_ARG;
    if (std::strcmp(tool->name, "ListRooms") == 0)
      result = modelList(runtime, core_domain::ModelKind::room, "rooms");
    else if (std::strcmp(tool->name, "ListGroups") == 0)
      result = modelList(runtime, core_domain::ModelKind::group, "groups");
    else if (std::strcmp(tool->name, "ListScenes") == 0)
      result = modelList(runtime, core_domain::ModelKind::scene, "scenes");
    else
      result = modelList(runtime, core_domain::ModelKind::automation, "automations");
  } else if (std::strcmp(tool->name, "EnableAutomation") == 0) {
    return setAutomationEnabled(runtime, arguments, true, result);
  } else if (std::strcmp(tool->name, "DisableAutomation") == 0) {
    return setAutomationEnabled(runtime, arguments, false, result);
  } else if (std::strcmp(tool->name, "RunAutomation") == 0) {
    return runAutomation(runtime, arguments, result);
  } else {
    core_domain::ModelKind kind{};
    bool create = false;
    bool update = false;
    bool remove = false;
    if (modelTool(*tool, kind, create, update, remove))
      return mutateModel(runtime, *tool, arguments, result);
  }
  if (std::strcmp(tool->name, "SetOnOff") == 0) {
    const char* allowed[]{"device_id", "endpoint_id", "on"};
    if (!hasOnlyKeys(arguments, allowed, std::size(allowed)))
      return ESP_ERR_INVALID_ARG;
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
    const bool level_command = std::strcmp(tool->name, "SetLevel") == 0;
    const char* allowed[]{"device_id", "endpoint_id", level_command ? "level" : "mireds",
                          "transition_time_ds"};
    if (!hasOnlyKeys(arguments, allowed, std::size(allowed)))
      return ESP_ERR_INVALID_ARG;
    std::uint64_t node_id = 0;
    std::uint16_t endpoint_id = 0;
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
      char* text = invocation == nullptr ? nullptr : cJSON_PrintUnformatted(invocation);
      cJSON_AddStringToObject(entry, "text",
                              text == nullptr ? "Tool execution was rejected." : text);
      cJSON_free(text);
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
