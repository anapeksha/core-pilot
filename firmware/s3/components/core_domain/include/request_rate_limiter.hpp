#pragma once

#include <cstddef>
#include <cstdint>

namespace core_domain {

struct RateLimitPolicy {
  uint32_t capacity;
  uint32_t refill_per_second;
};

struct RateLimitResult {
  bool allowed;
  uint32_t retry_after_seconds;
};

class RequestRateLimiter {
public:
  static constexpr size_t max_sources = 8;

  RequestRateLimiter(RateLimitPolicy global_policy, RateLimitPolicy source_policy);

  RateLimitResult allow(uint64_t source, uint64_t now_seconds);

private:
  struct Bucket {
    uint32_t tokens = 0;
    uint64_t updated_at = 0;
  };

  struct SourceBucket {
    uint64_t source = 0;
    Bucket bucket{};
    bool used = false;
  };

  static void refill(Bucket& bucket, RateLimitPolicy policy, uint64_t now_seconds);
  static uint32_t retry_after(const Bucket& bucket, RateLimitPolicy policy);

  RateLimitPolicy global_policy_;
  RateLimitPolicy source_policy_;
  Bucket global_;
  SourceBucket sources_[max_sources]{};
};

} // namespace core_domain
