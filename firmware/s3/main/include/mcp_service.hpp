#pragma once

#include "esp_err.h"

struct cJSON;

namespace core_pilot {

struct HttpRuntime;
class AiChatService;

/** Builds and invokes the bounded local MCP capability registry. */
class McpService final {
public:
  enum class Effect : unsigned char { read_only, physical, persistent, destructive };
  static esp_err_t initialize(const HttpRuntime& runtime);
  static esp_err_t setPermission(const HttpRuntime& runtime, const char* tool, bool enabled,
                                 const char* confirmation);
  static cJSON* tools();
  static cJSON* context(const HttpRuntime& runtime);
  // Checks availability and persisted permission. Effectful calls require approval from a
  // trusted local channel; model text and tool arguments can never provide that approval.
  static esp_err_t preflight(const char* tool, bool& mutating, bool& destructive);
  static esp_err_t preflight(const char* tool, Effect& effect);
  static bool requiresLocalApproval(Effect effect);
  static bool invocationAuthorized(Effect effect, bool approved_local);
  // External and model-driven callers may execute read-only tools only. Effectful tools are
  // rejected here even when enabled; approval text or tool arguments never grant authority.
  static esp_err_t invoke(const HttpRuntime& runtime, const char* tool, const cJSON* arguments,
                          cJSON*& result);
  // A null response with has_response=false is a valid JSON-RPC notification.
  static cJSON* jsonRpc(const HttpRuntime& runtime, const cJSON* request, bool& has_response);

private:
  friend class AiChatService;
  // Executes an effectful tool after the trusted local AI owner has independently bound and
  // approved the exact call. The friend restriction prevents HTTP/MCP transports from using it.
  static esp_err_t invokeApproved(const HttpRuntime& runtime, const char* tool,
                                  const cJSON* arguments, cJSON*& result);
};

} // namespace core_pilot
