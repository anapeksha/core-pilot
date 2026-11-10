#include "core_ota_policy.hpp"

#include <cstdint>
#include <unity.h>
#include <unity_test_runner.h>

using namespace core_pilot::ota_policy;

TEST_CASE("OTA digest parsing requires exact hexadecimal encoding", "[core_ota]") {
  std::uint8_t digest[2]{};
  TEST_ASSERT_TRUE(parse_digest("00Af", digest, sizeof digest));
  TEST_ASSERT_EQUAL_HEX8(0x00, digest[0]);
  TEST_ASSERT_EQUAL_HEX8(0xaf, digest[1]);
  TEST_ASSERT_FALSE(parse_digest("00ag", digest, sizeof digest));
  TEST_ASSERT_FALSE(parse_digest("", digest, sizeof digest));
  TEST_ASSERT_FALSE(parse_digest("0", digest, sizeof digest));
  TEST_ASSERT_FALSE(parse_digest("00", digest, sizeof digest));
  TEST_ASSERT_FALSE(parse_digest("00a", digest, sizeof digest));
  TEST_ASSERT_FALSE(parse_digest("00af00", digest, sizeof digest));
  TEST_ASSERT_FALSE(parse_digest(nullptr, digest, sizeof digest));
}

TEST_CASE("OTA semantic versions validate and order prereleases", "[core_ota]") {
  Version version{};
  TEST_ASSERT_TRUE(parse_version("1.2.3-rc.1+build.5", version));
  TEST_ASSERT_NOT_EQUAL(0, version.prerelease.size);
  TEST_ASSERT_TRUE(parse_version("1.2.3", version));
  TEST_ASSERT_EQUAL(0, version.prerelease.size);
  TEST_ASSERT_FALSE(parse_version("01.2.3", version));
  TEST_ASSERT_EQUAL(0, version.prerelease.size);
  TEST_ASSERT_FALSE(parse_version("1.2.3-01", version));
  TEST_ASSERT_TRUE(newer_version("1.2.3", "1.2.3-rc.1"));
  TEST_ASSERT_TRUE(newer_version("2.0.0-alpha", "1.99.99"));
  TEST_ASSERT_FALSE(newer_version("1.2.3+new", "1.2.3+old"));
  TEST_ASSERT_FALSE(newer_version("1.2", "1.1.9"));
}

TEST_CASE("OTA version text is bounded printable and nonblank", "[core_ota]") {
  TEST_ASSERT_TRUE(valid_text_version("1.2.3", 64));
  TEST_ASSERT_FALSE(valid_text_version("", 64));
  TEST_ASSERT_FALSE(valid_text_version("   ", 64));
  TEST_ASSERT_FALSE(valid_text_version("1\n2", 64));
  TEST_ASSERT_FALSE(valid_text_version("12345", 4));
}

TEST_CASE("OTA chunk validation enforces offset bounds and finality", "[core_ota]") {
  const std::uint8_t data[]{1, 2, 3, 4};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChunkError::ok),
                        static_cast<int>(validate_chunk(data, 4, 4, 0, 0, 8, false)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChunkError::ok),
                        static_cast<int>(validate_chunk(data, 4, 4, 4, 4, 8, true)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChunkError::invalid_argument),
                        static_cast<int>(validate_chunk(nullptr, 4, 4, 0, 0, 8, false)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChunkError::chunk_too_large),
                        static_cast<int>(validate_chunk(data, 4, 3, 0, 0, 8, false)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChunkError::offset_mismatch),
                        static_cast<int>(validate_chunk(data, 4, 4, 1, 0, 8, false)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChunkError::image_too_large),
                        static_cast<int>(validate_chunk(data, 4, 4, 8, 8, 8, true)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChunkError::final_mismatch),
                        static_cast<int>(validate_chunk(data, 4, 4, 0, 0, 4, false)));
}

TEST_CASE("OTA concurrent foreign replayed and stale sessions are rejected", "[core_ota]") {
  const std::uint8_t active[]{1, 2, 3, 4};
  const std::uint8_t concurrent_foreign[]{1, 2, 3, 5};
  const std::uint8_t stale_replayed[]{9, 8, 7, 6};
  const std::uint8_t missing[]{0, 0, 0, 0};
  TEST_ASSERT_TRUE(session_matches(active, active, sizeof active));
  TEST_ASSERT_FALSE(session_matches(concurrent_foreign, active, sizeof active));
  TEST_ASSERT_FALSE(session_matches(stale_replayed, active, sizeof active));
  TEST_ASSERT_FALSE(session_matches(missing, active, sizeof active));
  TEST_ASSERT_FALSE(session_matches(missing, missing, sizeof missing));
}

TEST_CASE("OTA sessions progress and digest comparison cover boundaries", "[core_ota]") {
  TEST_ASSERT_EQUAL_UINT8(0, progress_percent(1, 0));
  TEST_ASSERT_EQUAL_UINT8(33, progress_percent(1, 3));
  TEST_ASSERT_EQUAL_UINT8(100, progress_percent(3, 3));
  const std::uint8_t left[]{1, 2, 3};
  const std::uint8_t same[]{1, 2, 3};
  const std::uint8_t different[]{1, 2, 4};
  TEST_ASSERT_TRUE(digest_equal(left, same, sizeof left));
  TEST_ASSERT_FALSE(digest_equal(left, different, sizeof left));
}
