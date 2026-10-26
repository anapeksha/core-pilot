#include "core_smtp.hpp"
#include "lwip/inet.h"
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

TEST_CASE("SMTP plain mode permits only literal private LAN targets", "[core_smtp]") {
  TEST_ASSERT_TRUE(plain_lan_target_allowed("192.168.1.2"));
  TEST_ASSERT_TRUE(plain_lan_target_allowed("10.0.0.1"));
  TEST_ASSERT_TRUE(plain_lan_target_allowed("fd00::1"));
  TEST_ASSERT_FALSE(plain_lan_target_allowed("smtp.local"));
  TEST_ASSERT_FALSE(plain_lan_target_allowed("8.8.8.8"));
  TEST_ASSERT_FALSE(plain_lan_target_allowed("127.0.0.1"));
  TEST_ASSERT_FALSE(plain_lan_target_allowed("169.254.1.1"));
  TEST_ASSERT_FALSE(plain_lan_target_allowed("::1"));
  TEST_ASSERT_FALSE(plain_lan_target_allowed("fe80::1"));
}

TEST_CASE("SMTP ports are fixed by transport security", "[core_smtp]") {
  TEST_ASSERT_TRUE(transport_port_allowed(SecurityMode::Tls, 465));
  TEST_ASSERT_TRUE(transport_port_allowed(SecurityMode::StartTls, 587));
  TEST_ASSERT_TRUE(transport_port_allowed(SecurityMode::PlainLanOnly, 25));
  TEST_ASSERT_FALSE(transport_port_allowed(SecurityMode::Tls, 25));
  TEST_ASSERT_FALSE(transport_port_allowed(SecurityMode::StartTls, 465));
  TEST_ASSERT_FALSE(transport_port_allowed(SecurityMode::PlainLanOnly, 2525));
}

TEST_CASE("SMTP resolved destination policy rejects special-use addresses", "[core_smtp]") {
  const auto allowed = [](SecurityMode mode, int family, const char* text) {
    std::uint8_t address[16]{};
    TEST_ASSERT_EQUAL(1, inet_pton(family, text, address));
    return resolved_target_allowed(mode, family, address,
                                   family == AF_INET ? sizeof(in_addr) : sizeof(in6_addr));
  };
  TEST_ASSERT_TRUE(allowed(SecurityMode::Tls, AF_INET, "8.8.8.8"));
  TEST_ASSERT_FALSE(allowed(SecurityMode::Tls, AF_INET, "10.0.0.1"));
  TEST_ASSERT_FALSE(allowed(SecurityMode::Tls, AF_INET, "100.64.0.1"));
  TEST_ASSERT_FALSE(allowed(SecurityMode::Tls, AF_INET, "192.0.2.1"));
  TEST_ASSERT_FALSE(allowed(SecurityMode::Tls, AF_INET6, "2001:db8::1"));
  TEST_ASSERT_TRUE(allowed(SecurityMode::Tls, AF_INET6, "2606:4700:4700::1111"));
  TEST_ASSERT_TRUE(allowed(SecurityMode::PlainLanOnly, AF_INET, "172.16.0.1"));
  TEST_ASSERT_FALSE(allowed(SecurityMode::PlainLanOnly, AF_INET, "169.254.0.1"));
}

TEST_CASE("SMTP retains credentials only for an unchanged endpoint", "[core_smtp]") {
  Config current{};
  current.port = 587;
  current.security = SecurityMode::StartTls;
  std::strcpy(current.host, "smtp.example.com");
  std::strcpy(current.username, "sender");
  Config update = current;
  TEST_ASSERT_TRUE(same_credential_endpoint(current, update));
  update.port = 465;
  TEST_ASSERT_FALSE(same_credential_endpoint(current, update));
  update = current;
  update.security = SecurityMode::Tls;
  TEST_ASSERT_FALSE(same_credential_endpoint(current, update));
  update = current;
  std::strcpy(update.host, "attacker.example.com");
  TEST_ASSERT_FALSE(same_credential_endpoint(current, update));
  update = current;
  std::strcpy(update.username, "attacker");
  TEST_ASSERT_FALSE(same_credential_endpoint(current, update));
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
