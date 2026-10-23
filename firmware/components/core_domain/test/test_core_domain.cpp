#include "core_domain.hpp"

#include <atomic>
#include <cJSON.h>
#include <cstring>
#include <unity.h>
#include <unity_test_runner.h>

using namespace core_domain;

TEST_CASE("API JSON bounds, trailing data and origin are rejected", "[core_domain]") {
  cJSON* json = nullptr;
  TEST_ASSERT_EQUAL(static_cast<int>(ApiError::body_too_large),
                    static_cast<int>(parse_json("{}", 2, 1, json)));
  TEST_ASSERT_NULL(json);
  char oversized[max_ai_body + 2]{};
  TEST_ASSERT_EQUAL(
      static_cast<int>(ApiError::body_too_large),
      static_cast<int>(parse_json(oversized, sizeof oversized, sizeof oversized, json)));
  TEST_ASSERT_NULL(json);
  TEST_ASSERT_EQUAL(static_cast<int>(ApiError::invalid_json),
                    static_cast<int>(parse_json("{} false", 8, max_json_body, json)));
  TEST_ASSERT_NULL(json);
  TEST_ASSERT_EQUAL(static_cast<int>(ApiError::ok),
                    static_cast<int>(parse_json("{} \n", 4, max_json_body, json)));
  cJSON_Delete(json);
  TEST_ASSERT_EQUAL(static_cast<int>(ApiError::ok),
                    static_cast<int>(same_origin_mutation_allowed(nullptr, "corepilot.local")));
  TEST_ASSERT_EQUAL(
      static_cast<int>(ApiError::ok),
      static_cast<int>(same_origin_mutation_allowed("http://corepilot.local", "corepilot.local")));
  TEST_ASSERT_EQUAL(
      static_cast<int>(ApiError::cross_origin_mutation),
      static_cast<int>(same_origin_mutation_allowed("https://corepilot.local", "corepilot.local")));
}

TEST_CASE("common API DTOs preserve wire keys and numeric bounds", "[core_domain]") {
  char buffer[256]{};
  TEST_ASSERT_TRUE(serialize_mutation_response(false, error_code::OTA_CHUNK_REJECTED, "", buffer,
                                               sizeof buffer));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"status\":\"OTA_CHUNK_REJECTED\""));
  cJSON* json = cJSON_Parse("{\"before_sequence\":4294967295,\"limit\":255}");
  ActivityPageRequest request{};
  TEST_ASSERT_EQUAL(static_cast<int>(ApiError::ok),
                    static_cast<int>(parse_activity_page_request(json, request)));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, request.before_sequence);
  TEST_ASSERT_EQUAL_UINT8(255, request.limit);
  cJSON_Delete(json);
  json = cJSON_Parse("{\"limit\":256}");
  TEST_ASSERT_EQUAL(static_cast<int>(ApiError::invalid_json),
                    static_cast<int>(parse_activity_page_request(json, request)));
  TEST_ASSERT_EQUAL_UINT8(255,
                          request.limit); // failure leaves output unchanged
  cJSON_Delete(json);
}

TEST_CASE("connection slots release on scope exit and cannot exceed three", "[core_domain]") {
  std::atomic<uint8_t> active{0};
  {
    HttpConnectionLease a(active), b(active), c(active), denied(active);
    TEST_ASSERT_TRUE(a.acquired());
    TEST_ASSERT_TRUE(b.acquired());
    TEST_ASSERT_TRUE(c.acquired());
    TEST_ASSERT_FALSE(denied.acquired());
    TEST_ASSERT_EQUAL_UINT8(3, active.load());
  }
  TEST_ASSERT_EQUAL_UINT8(0, active.load());
}

TEST_CASE("model DTOs normalize, deduplicate and enforce action bounds", "[core_domain]") {
  cJSON* json = cJSON_Parse("{\"id\":\" room_1 \",\"name\":\" Kitchen "
                            "\",\"actions\":[\"on|lamp|1\",\"on|lamp|1\"]}");
  ModelUpsert model{};
  TEST_ASSERT_EQUAL(static_cast<int>(ModelError::ok),
                    static_cast<int>(parse_model_upsert(json, ModelKind::scene, model)));
  TEST_ASSERT_EQUAL_STRING("room_1", model.id);
  TEST_ASSERT_EQUAL_STRING("Kitchen", model.name);
  TEST_ASSERT_EQUAL_UINT32(1, model.detail_count);
  char serialized[256]{};
  TEST_ASSERT_TRUE(serialize_model(model, ModelKind::scene, serialized, sizeof serialized));
  TEST_ASSERT_NOT_NULL(std::strstr(serialized, "\"actions\":[\"on|lamp|1\"]"));
  TEST_ASSERT_FALSE(serialize_model(model, ModelKind::scene, serialized, 4));
  TEST_ASSERT_EQUAL_CHAR('\0', serialized[0]);
  model.detail_count = max_members + 1;
  TEST_ASSERT_FALSE(serialize_model(model, ModelKind::scene, serialized, sizeof serialized));
  model.detail_count = 1;
  cJSON_Delete(json);
  json = cJSON_Parse("{\"name\":\"X\",\"actions\":[\"delay|60001\"]}");
  TEST_ASSERT_EQUAL(static_cast<int>(ModelError::invalid_detail),
                    static_cast<int>(parse_model_upsert(json, ModelKind::scene, model)));
  cJSON_Delete(json);
  json = cJSON_Parse("{\"name\":\"X\",\"actions\":[\"delay|0\",\"delay|1\","
                     "\"delay|2\",\"delay|3\",\"delay|4\",\"delay|5\",\"delay|"
                     "6\",\"delay|7\",\"delay|8\"]}");
  TEST_ASSERT_EQUAL(static_cast<int>(ModelError::too_many_actions),
                    static_cast<int>(parse_model_upsert(json, ModelKind::scene, model)));
  cJSON_Delete(json);
  char id[max_id + 1]{};
  TEST_ASSERT_EQUAL(static_cast<int>(ModelError::invalid_id),
                    static_cast<int>(normalize_id("bad%id", id)));
}

TEST_CASE("automation grammar rejects invalid dates, masks, delays and levels", "[core_domain]") {
  TriggerSpec trigger{};
  TEST_ASSERT_TRUE(parse_trigger_spec("once|2024-02-29|09:30|+840", trigger));
  TEST_ASSERT_FALSE(parse_trigger_spec("once|2023-02-29|09:30|+0", trigger));
  TEST_ASSERT_FALSE(parse_trigger_spec("weekly|128|09:30|+0", trigger));
  TEST_ASSERT_TRUE(parse_trigger_spec("weekly|1|09:30|-720", trigger));
  TEST_ASSERT_TRUE(schedule_matches(trigger, {2024, 2, 29, 9, 30, 1}));
  TEST_ASSERT_FALSE(schedule_matches(trigger, {2024, 2, 29, 9, 30, 2}));
  ActionSpec action{};
  TEST_ASSERT_TRUE(parse_action_spec("level|lamp%1|100", action));
  TEST_ASSERT_FALSE(parse_action_spec("level|lamp|101", action));
  TEST_ASSERT_FALSE(parse_action_spec("delay|60001", action));
  TEST_ASSERT_FALSE(parse_action_spec("on|lamp|true", action));
}

TEST_CASE("OTA metadata and chunk offsets, padding and size are bounded", "[core_domain]") {
  cJSON* json =
      cJSON_Parse("{\"version\":\"v1\",\"size_bytes\":3,\"sha256_hex\":"
                  "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}");
  OtaPrepare prepare{};
  TEST_ASSERT_EQUAL(static_cast<int>(OtaError::ok),
                    static_cast<int>(parse_ota_prepare(json, prepare)));
  cJSON_Delete(json);
  char encoded[512]{};
  TEST_ASSERT_TRUE(serialize_ota_prepare(prepare, encoded, sizeof encoded));
  TEST_ASSERT_NOT_NULL(std::strstr(encoded, "\"size_bytes\":3"));
  prepare.size_bytes = max_ota_image + 1;
  TEST_ASSERT_FALSE(serialize_ota_prepare(prepare, encoded, sizeof encoded));
  TEST_ASSERT_EQUAL(static_cast<int>(OtaError::image_too_large),
                    static_cast<int>(validate_ota_prepare(prepare)));
  json = cJSON_Parse("{\"offset\":0,\"data_base64\":\"AQID\",\"final_chunk\":true}");
  OtaChunk chunk{};
  size_t decoded = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(OtaError::ok), static_cast<int>(parse_ota_chunk(json, chunk)));
  TEST_ASSERT_EQUAL(static_cast<int>(OtaError::ok),
                    static_cast<int>(validate_ota_chunk(chunk, 0, 3, decoded)));
  TEST_ASSERT_EQUAL_UINT32(3, decoded);
  TEST_ASSERT_TRUE(serialize_ota_chunk(chunk, encoded, sizeof encoded));
  TEST_ASSERT_NOT_NULL(std::strstr(encoded, "\"data_base64\":\"AQID\""));
  TEST_ASSERT_EQUAL(static_cast<int>(OtaError::offset_mismatch),
                    static_cast<int>(validate_ota_chunk(chunk, 1, 3, decoded)));
  cJSON_Delete(json);
  std::strcpy(chunk.data_base64, "AR=="); // noncanonical unused bits
  TEST_ASSERT_EQUAL(static_cast<int>(OtaError::chunk_too_large),
                    static_cast<int>(validate_ota_chunk(chunk, 0, 3, decoded)));
  std::strcpy(chunk.data_base64, "AQ==");
  TEST_ASSERT_EQUAL(static_cast<int>(OtaError::image_too_large),
                    static_cast<int>(validate_ota_chunk(chunk, 0, 3, decoded)));
}
