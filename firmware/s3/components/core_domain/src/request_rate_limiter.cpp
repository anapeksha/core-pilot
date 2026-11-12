#include "request_rate_limiter.hpp"

#include <algorithm>
#include <limits>

namespace core_domain {
namespace {
RateLimitPolicy valid_policy(RateLimitPolicy policy) {
  policy.capacity = std::max(policy.capacity, uint32_t{1});
  policy.refill_per_second = std::max(policy.refill_per_second, uint32_t{1});
  return policy;
}
} // namespace

RequestRateLimiter::RequestRateLimiter(RateLimitPolicy global_policy, RateLimitPolicy source_policy)
    : global_policy_(valid_policy(global_policy)), source_policy_(valid_policy(source_policy)),
      global_{global_policy_.capacity, 0} {}

void RequestRateLimiter::refill(Bucket& bucket, RateLimitPolicy policy, uint64_t now_seconds) {
  if (now_seconds <= bucket.updated_at) {
    return;
  }

  const uint64_t elapsed = now_seconds - bucket.updated_at;
  const uint32_t missing = policy.capacity - bucket.tokens;
  if (elapsed >= (missing + uint64_t{policy.refill_per_second} - 1) / policy.refill_per_second) {
    bucket.tokens = policy.capacity;
  } else {
    bucket.tokens += static_cast<uint32_t>(elapsed * policy.refill_per_second);
  }
  bucket.updated_at = now_seconds;
}

uint32_t RequestRateLimiter::retry_after(const Bucket& bucket, RateLimitPolicy policy) {
  if (bucket.tokens != 0) {
    return 1;
  }
  const uint64_t seconds = (uint64_t{1} + policy.refill_per_second - 1) / policy.refill_per_second;
  return static_cast<uint32_t>(
      std::clamp(seconds, uint64_t{1}, uint64_t{std::numeric_limits<uint32_t>::max()}));
}

RateLimitResult RequestRateLimiter::allow(uint64_t source, uint64_t now_seconds) {
  refill(global_, global_policy_, now_seconds);

  SourceBucket* selected = nullptr;
  SourceBucket* reusable = nullptr;
  SourceBucket* oldest = nullptr;
  for (auto& entry : sources_) {
    if (entry.used) {
      refill(entry.bucket, source_policy_, now_seconds);
      if (entry.source == source) {
        selected = &entry;
        break;
      }
      if (entry.bucket.tokens == source_policy_.capacity &&
          (reusable == nullptr || entry.bucket.updated_at < reusable->bucket.updated_at)) {
        reusable = &entry;
      }
      if (oldest == nullptr || entry.bucket.updated_at < oldest->bucket.updated_at)
        oldest = &entry;
    } else if (reusable == nullptr) {
      reusable = &entry;
    }
  }

  if (selected == nullptr) {
    selected = reusable != nullptr ? reusable : oldest;
    selected->source = source;
    selected->bucket = {source_policy_.capacity, now_seconds};
    selected->used = true;
  }

  if (global_.tokens == 0 || selected->bucket.tokens == 0) {
    uint32_t retry = 1;
    if (global_.tokens == 0) {
      retry = std::max(retry, retry_after(global_, global_policy_));
    }
    if (selected->bucket.tokens == 0) {
      retry = std::max(retry, retry_after(selected->bucket, source_policy_));
    }
    return {false, retry};
  }

  --global_.tokens;
  --selected->bucket.tokens;
  return {true, 0};
}

} // namespace core_domain
