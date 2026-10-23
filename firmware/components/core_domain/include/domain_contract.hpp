#pragma once

#include "core_domain.hpp"

namespace core_domain {
// A contract describes wire data only. The caller owns the returned cJSON tree.
struct RouteContract {
  const char* method;
  const char* path;
  const char* request_shape; // null for bodyless GET/POST
  const char* response_shape;
};

const RouteContract* find_contract(const char* method, const char* path);
size_t contract_count();
const RouteContract* contract_at(size_t index);
// Validates a length-delimited request, including nested DTOs and firmware
// bounds. On failure *output is null; no service or transport is invoked.
ApiError parse_contract_request(const RouteContract& route, const char* bytes, size_t length,
                                cJSON*& output);
// Validates and prints a response supplied by the service layer. No ownership
// of input is taken. On failure output is empty.
bool serialize_contract_response(const RouteContract& route, const cJSON* value, char* output,
                                 size_t capacity);
} // namespace core_domain
