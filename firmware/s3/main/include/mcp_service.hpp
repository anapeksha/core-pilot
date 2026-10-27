#pragma once

#include "esp_err.h"

struct cJSON;

namespace core_pilot {

struct HttpRuntime;

/** Builds and invokes the bounded local MCP capability registry. */
class McpService final {
public:
  static esp_err_t initialize(const HttpRuntime& runtime);
  static esp_err_t setPermission(const HttpRuntime& runtime, const char* tool, bool enabled);
  static cJSON* tools();
  static cJSON* context(const HttpRuntime& runtime);
  static esp_err_t invoke(const HttpRuntime& runtime, const char* tool, const cJSON* arguments,
                          cJSON*& result);
  // A null response with has_response=false is a valid JSON-RPC notification.
  static cJSON* jsonRpc(const HttpRuntime& runtime, const cJSON* request, bool& has_response);
};

} // namespace core_pilot
