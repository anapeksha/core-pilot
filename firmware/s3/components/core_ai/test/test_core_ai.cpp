#include "core_ai.hpp"

#include <cmath>
#include <cstring>
#include <unity.h>
#include <unity_test_runner.h>

using namespace core_pilot::ai;

namespace {

ProviderConfig openai_config() {
  ProviderConfig config{};
  std::strcpy(config.base_url, "https://api.openai.com/v1");
  std::strcpy(config.model, "gpt-test");
  config.api_key_length = 1;
  config.api_key[0] = 'x';
  return config;
}

} // namespace

TEST_CASE("AI memory identifiers enforce exact byte and alphabet bounds", "[core_ai]") {
  char exact[kMaxMemoryIdBytes + 1]{};
  std::memset(exact, 'a', kMaxMemoryIdBytes);
  TEST_ASSERT_TRUE(valid_memory_id(exact));
  char oversized[kMaxMemoryIdBytes + 2]{};
  std::memset(oversized, 'a', kMaxMemoryIdBytes + 1);
  TEST_ASSERT_FALSE(valid_memory_id(oversized));
  TEST_ASSERT_TRUE(valid_memory_id("room_1-history"));
  TEST_ASSERT_FALSE(valid_memory_id(""));
  TEST_ASSERT_FALSE(valid_memory_id(nullptr));
  TEST_ASSERT_FALSE(valid_memory_id("room/history"));
  TEST_ASSERT_FALSE(valid_memory_id("room name"));
}

TEST_CASE("AI prompt paths allow portable path syntax within byte bounds", "[core_ai]") {
  TEST_ASSERT_TRUE(valid_prompt_path("system/baseline_v1.md"));
  TEST_ASSERT_TRUE(valid_prompt_path("a.b-c_d/e"));
  TEST_ASSERT_FALSE(valid_prompt_path(""));
  TEST_ASSERT_FALSE(valid_prompt_path("../prompt:bad"));
  TEST_ASSERT_FALSE(valid_prompt_path("prompt name.md"));
  char oversized[kMaxPromptPathBytes + 2]{};
  std::memset(oversized, 'p', kMaxPromptPathBytes + 1);
  TEST_ASSERT_FALSE(valid_prompt_path(oversized));
}

TEST_CASE("AI provider usability binds supported HTTPS origins and auth", "[core_ai]") {
  ProviderConfig config = openai_config();
  TEST_ASSERT_TRUE(provider_config_usable(config));
  std::strcpy(config.base_url, "https://API.OPENAI.COM:443/v1");
  TEST_ASSERT_TRUE(provider_config_usable(config));
  std::strcpy(config.base_url, "http://api.openai.com/v1");
  TEST_ASSERT_FALSE(provider_config_usable(config));
  std::strcpy(config.base_url, "https://api.openai.com:444/v1");
  TEST_ASSERT_FALSE(provider_config_usable(config));
  std::strcpy(config.base_url, "https://user@api.openai.com/v1");
  TEST_ASSERT_FALSE(provider_config_usable(config));
  config = openai_config();
  config.auth_type = AuthType::ApiKey;
  TEST_ASSERT_FALSE(provider_config_usable(config));
  config = openai_config();
  config.api_key_length = 0;
  TEST_ASSERT_FALSE(provider_config_usable(config));
}

TEST_CASE("AI provider numeric and finite-value boundaries are enforced", "[core_ai]") {
  ProviderConfig config = openai_config();
  config.max_output_tokens = 1;
  config.timeout_ms = 1000;
  TEST_ASSERT_TRUE(provider_config_usable(config));
  config.max_output_tokens = 8192;
  config.timeout_ms = 120000;
  TEST_ASSERT_TRUE(provider_config_usable(config));
  config.max_output_tokens = 8193;
  TEST_ASSERT_FALSE(provider_config_usable(config));
  config = openai_config();
  config.timeout_ms = 999;
  TEST_ASSERT_FALSE(provider_config_usable(config));
  config = openai_config();
  config.temperature = NAN;
  TEST_ASSERT_FALSE(provider_config_usable(config));
  config.has_temperature = false;
  TEST_ASSERT_TRUE(provider_config_usable(config));
}

TEST_CASE("AI redacted credentials stay within the same provider scope", "[core_ai]") {
  ProviderConfig stored = openai_config();
  ProviderConfig incoming = stored;
  std::strcpy(incoming.base_url, "https://API.OPENAI.COM:443/other");
  std::strcpy(incoming.model, "another-model");
  TEST_ASSERT_TRUE(same_provider_credential_scope(incoming, stored));
  std::strcpy(incoming.base_url, "https://api.anthropic.com/v1");
  TEST_ASSERT_FALSE(same_provider_credential_scope(incoming, stored));
  incoming = stored;
  incoming.auth_type = AuthType::ApiKey;
  TEST_ASSERT_FALSE(same_provider_credential_scope(incoming, stored));
  incoming = stored;
  incoming.backend = ProviderBackend::AnthropicCompatible;
  TEST_ASSERT_FALSE(same_provider_credential_scope(incoming, stored));
}
