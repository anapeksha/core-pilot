#include "core_smtp.hpp"
#include "unity.h"

#include <cstring>

using namespace core_pilot::smtp;

TEST_CASE("SMTP reply parser accepts bounded multiline replies", "[core_smtp]") {
  ReplyParser parser;
  constexpr char first[] = "250-mail.example\r\n250-STARTTLS\r\n";
  constexpr char second[] = "250 AUTH LOGIN\r\n";
  TEST_ASSERT_EQUAL(static_cast<int>(ReplyParser::Result::NeedMore),
                    static_cast<int>(parser.consume(reinterpret_cast<const std::uint8_t*>(first),
                                                    sizeof(first) - 1)));
  TEST_ASSERT_EQUAL(static_cast<int>(ReplyParser::Result::Complete),
                    static_cast<int>(parser.consume(reinterpret_cast<const std::uint8_t*>(second),
                                                    sizeof(second) - 1)));
  TEST_ASSERT_EQUAL(250, parser.code());
}

TEST_CASE("SMTP reply parser rejects malformed line endings and mixed codes", "[core_smtp]") {
  ReplyParser parser;
  constexpr char lf[] = "220 hello\n";
  TEST_ASSERT_EQUAL(
      static_cast<int>(ReplyParser::Result::Invalid),
      static_cast<int>(parser.consume(reinterpret_cast<const std::uint8_t*>(lf), sizeof(lf) - 1)));
  parser.reset();
  constexpr char mixed[] = "250-first\r\n220 done\r\n";
  TEST_ASSERT_EQUAL(static_cast<int>(ReplyParser::Result::Invalid),
                    static_cast<int>(parser.consume(reinterpret_cast<const std::uint8_t*>(mixed),
                                                    sizeof(mixed) - 1)));
}

TEST_CASE("SMTP DATA encoding normalizes and dot stuffs", "[core_smtp]") {
  constexpr char body[] = ".first\nsecond\rthird\r\n..fourth";
  constexpr char expected[] = "..first\r\nsecond\r\nthird\r\n...fourth\r\n";
  std::uint8_t output[128]{};
  std::size_t written{};
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::Ok),
      static_cast<int>(encode_data_body(body, sizeof(body) - 1, output, sizeof(output), written)));
  TEST_ASSERT_EQUAL(sizeof(expected) - 1, written);
  TEST_ASSERT_EQUAL_MEMORY(expected, output, written);
}

TEST_CASE("SMTP plain mode permits only literal local targets", "[core_smtp]") {
  TEST_ASSERT_TRUE(plain_lan_target_allowed("192.168.1.2"));
  TEST_ASSERT_TRUE(plain_lan_target_allowed("10.0.0.1"));
  TEST_ASSERT_TRUE(plain_lan_target_allowed("fd00::1"));
  TEST_ASSERT_FALSE(plain_lan_target_allowed("smtp.local"));
  TEST_ASSERT_FALSE(plain_lan_target_allowed("8.8.8.8"));
}

TEST_CASE("SMTP subjects reject CR and LF header injection", "[core_smtp]") {
  TEST_ASSERT_FALSE(contains_cr_or_lf("Monthly report", kMaxSubjectBytes));
  TEST_ASSERT_TRUE(contains_cr_or_lf("Report\rBCC: attacker@example.com", kMaxSubjectBytes));
  TEST_ASSERT_TRUE(contains_cr_or_lf("Report\nBCC: attacker@example.com", kMaxSubjectBytes));
  TEST_ASSERT_TRUE(contains_cr_or_lf("Report\r\nBCC: attacker@example.com", kMaxSubjectBytes));
  char oversized[kMaxSubjectBytes + 2]{};
  std::memset(oversized, 'x', sizeof(oversized) - 1);
  TEST_ASSERT_TRUE(contains_cr_or_lf(oversized, kMaxSubjectBytes));
}

TEST_CASE("SMTP address and injection checks are strict", "[core_smtp]") {
  TEST_ASSERT_TRUE(valid_email_address("user@example.com"));
  TEST_ASSERT_FALSE(valid_email_address("user@example.com\r\nBCC:x@y.test"));
  TEST_ASSERT_FALSE(valid_email_address("missing-at.example.com"));
  TEST_ASSERT_TRUE(contains_cr_or_lf("safe\nunsafe", 32));
}
