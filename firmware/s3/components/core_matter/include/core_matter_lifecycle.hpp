#pragma once

#include <cstddef>
#include <cstdint>

namespace core_pilot::detail {

enum class SubscriptionPhase : std::uint8_t {
  empty,
  configured,
  subscribing,
  active,
  recovering,
  shutting_down,
};

struct EndpointLifecycle final {
  std::uint16_t endpoint_id;
  bool configured;
  bool active;
  bool report_known;
  bool on;
};

struct NodeLifecycle final {
  std::uint64_t node_id;
  std::uint32_t generation;
  SubscriptionPhase phase;
  bool case_ready;
  bool recovery_pending;
  EndpointLifecycle endpoints[16];
};

template <std::size_t NodeCapacity = 8, std::size_t EndpointCapacity = 16>
class LifecycleTable final {
public:
  static_assert(NodeCapacity > 0 && EndpointCapacity > 0 && EndpointCapacity <= 16);

  bool configure(std::uint64_t node_id, const std::uint16_t* endpoints, std::size_t count) {
    if (node_id == 0 || endpoints == nullptr || count == 0 || count > EndpointCapacity)
      return false;
    for (std::size_t i = 0; i < count; ++i) {
      if (endpoints[i] == 0)
        return false;
      for (std::size_t j = 0; j < i; ++j) {
        if (endpoints[j] == endpoints[i])
          return false;
      }
    }
    NodeLifecycle* node = find(node_id);
    if (node == nullptr) {
      for (auto& candidate : nodes_) {
        if (candidate.node_id == 0) {
          node = &candidate;
          break;
        }
      }
    }
    if (node == nullptr)
      return false;
    const std::uint32_t generation = nextGeneration(node->generation);
    *node = {};
    node->node_id = node_id;
    node->generation = generation;
    node->phase = SubscriptionPhase::configured;
    for (std::size_t i = 0; i < count; ++i)
      node->endpoints[i] = {endpoints[i], true, false, false, false};
    return true;
  }

  std::uint32_t beginSubscribe(std::uint64_t node_id) {
    NodeLifecycle* node = find(node_id);
    if (node == nullptr || node->phase == SubscriptionPhase::shutting_down)
      return 0;
    node->generation = nextGeneration(node->generation);
    node->phase = SubscriptionPhase::subscribing;
    node->case_ready = false;
    node->recovery_pending = false;
    for (auto& endpoint : node->endpoints)
      endpoint.active = false;
    return node->generation;
  }

  bool established(std::uint64_t node_id, std::uint32_t generation) {
    NodeLifecycle* node = current(node_id, generation);
    if (node == nullptr || node->phase != SubscriptionPhase::subscribing)
      return false;
    node->phase = SubscriptionPhase::active;
    node->case_ready = true;
    for (auto& endpoint : node->endpoints) {
      if (endpoint.configured)
        endpoint.active = true;
    }
    return true;
  }

  bool report(std::uint64_t node_id, std::uint32_t generation, std::uint16_t endpoint_id, bool on) {
    NodeLifecycle* node = current(node_id, generation);
    if (node == nullptr || node->phase == SubscriptionPhase::shutting_down)
      return false;
    for (auto& endpoint : node->endpoints) {
      if (endpoint.configured && endpoint.endpoint_id == endpoint_id) {
        endpoint.report_known = true;
        endpoint.on = on;
        return true;
      }
    }
    return false;
  }

  bool terminated(std::uint64_t node_id, std::uint32_t generation, bool recover) {
    NodeLifecycle* node = current(node_id, generation);
    if (node == nullptr)
      return false;
    node->case_ready = false;
    node->recovery_pending = recover && node->phase != SubscriptionPhase::shutting_down;
    node->phase =
        node->recovery_pending ? SubscriptionPhase::recovering : SubscriptionPhase::configured;
    for (auto& endpoint : node->endpoints) {
      endpoint.active = false;
      endpoint.report_known = false;
    }
    return true;
  }

  std::uint32_t beginShutdown(std::uint64_t node_id) {
    if (node_id == 0)
      return 0;
    NodeLifecycle* node = find(node_id);
    if (node == nullptr) {
      for (auto& candidate : nodes_) {
        if (candidate.node_id == 0) {
          node = &candidate;
          node->node_id = node_id;
          break;
        }
      }
    }
    if (node == nullptr)
      return 0;
    node->generation = nextGeneration(node->generation);
    node->phase = SubscriptionPhase::shutting_down;
    node->case_ready = false;
    node->recovery_pending = false;
    for (auto& endpoint : node->endpoints) {
      endpoint.active = false;
      endpoint.report_known = false;
    }
    return node->generation;
  }

  bool abandonShutdown(std::uint64_t node_id, std::uint32_t generation) {
    NodeLifecycle* node = current(node_id, generation);
    if (node == nullptr || node->phase != SubscriptionPhase::shutting_down)
      return false;
    node->generation = nextGeneration(node->generation);
    node->phase = SubscriptionPhase::recovering;
    node->recovery_pending = true;
    return true;
  }

  bool erase(std::uint64_t node_id, std::uint32_t generation) {
    NodeLifecycle* node = current(node_id, generation);
    if (node == nullptr || node->phase != SubscriptionPhase::shutting_down)
      return false;
    *node = {};
    return true;
  }

  void connectivityRestored() {
    for (auto& node : nodes_) {
      if (node.node_id != 0 && node.phase == SubscriptionPhase::recovering)
        node.recovery_pending = true;
    }
  }

  bool takeRecovery(std::uint64_t& node_id) {
    for (auto& node : nodes_) {
      if (node.recovery_pending && node.phase == SubscriptionPhase::recovering) {
        node.recovery_pending = false;
        node_id = node.node_id;
        return true;
      }
    }
    return false;
  }

  bool requeueRecovery(std::uint64_t node_id) {
    NodeLifecycle* node = find(node_id);
    if (node == nullptr || node->phase != SubscriptionPhase::recovering)
      return false;
    node->recovery_pending = true;
    return true;
  }

  const NodeLifecycle* snapshot(std::uint64_t node_id) const {
    return find(node_id);
  }

  const EndpointLifecycle* endpointSnapshot(std::uint64_t node_id,
                                            std::uint16_t endpoint_id) const {
    const NodeLifecycle* node = find(node_id);
    if (node == nullptr)
      return nullptr;
    for (const auto& endpoint : node->endpoints) {
      if (endpoint.configured && endpoint.endpoint_id == endpoint_id)
        return &endpoint;
    }
    return nullptr;
  }

  std::size_t nodeCount() const {
    std::size_t count = 0;
    for (const auto& node : nodes_)
      count += node.node_id != 0 ? 1U : 0U;
    return count;
  }

  std::size_t configuredEndpointCount() const {
    std::size_t count = 0;
    for (const auto& node : nodes_)
      for (const auto& endpoint : node.endpoints)
        count += endpoint.configured ? 1U : 0U;
    return count;
  }

  std::size_t activeSubscriptionCount() const {
    std::size_t count = 0;
    for (const auto& node : nodes_)
      count += node.phase == SubscriptionPhase::active ? 1U : 0U;
    return count;
  }

  bool anyCaseReady() const {
    for (const auto& node : nodes_)
      if (node.case_ready)
        return true;
    return false;
  }

private:
  static std::uint32_t nextGeneration(std::uint32_t generation) {
    ++generation;
    return generation == 0 ? 1 : generation;
  }

  NodeLifecycle* find(std::uint64_t node_id) {
    for (auto& node : nodes_)
      if (node.node_id == node_id)
        return &node;
    return nullptr;
  }

  const NodeLifecycle* find(std::uint64_t node_id) const {
    for (const auto& node : nodes_)
      if (node.node_id == node_id)
        return &node;
    return nullptr;
  }

  NodeLifecycle* current(std::uint64_t node_id, std::uint32_t generation) {
    NodeLifecycle* node = find(node_id);
    return node != nullptr && generation != 0 && node->generation == generation ? node : nullptr;
  }

  NodeLifecycle nodes_[NodeCapacity]{};
};

} // namespace core_pilot::detail
