#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace core_pilot::detail {

template <typename Entry, std::size_t Capacity> class TerminalHistory final {
public:
  static_assert(Capacity != 0);
  static_assert(std::is_trivially_copyable_v<Entry>);

  bool append(std::uint32_t operation_id, const Entry& entry) {
    Entry ignored{};
    if (find(operation_id, ignored)) {
      return false;
    }
    entries_[next_] = entry;
    operation_ids_[next_] = operation_id;
    next_ = (next_ + 1) % Capacity;
    if (size_ != Capacity) {
      ++size_;
    }
    return true;
  }

  bool find(std::uint32_t operation_id, Entry& entry) const {
    for (std::size_t offset = 0; offset < size_; ++offset) {
      const std::size_t index = (next_ + Capacity - 1 - offset) % Capacity;
      if (operation_ids_[index] == operation_id) {
        entry = entries_[index];
        return true;
      }
    }
    return false;
  }

  std::size_t after(std::uint64_t sequence, Entry* entries, std::size_t capacity) const {
    if (entries == nullptr || capacity == 0) {
      return 0;
    }
    std::size_t count = 0;
    const std::size_t oldest = (next_ + Capacity - size_) % Capacity;
    for (std::size_t offset = 0; offset < size_ && count < capacity; ++offset) {
      const Entry& entry = entries_[(oldest + offset) % Capacity];
      if (entry.sequence > sequence) {
        entries[count++] = entry;
      }
    }
    return count;
  }

private:
  Entry entries_[Capacity]{};
  std::uint32_t operation_ids_[Capacity]{};
  std::size_t next_{0};
  std::size_t size_{0};
};

} // namespace core_pilot::detail
