#include "transport/mux/fragment_reassembly.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace veil::mux {

FragmentReassembly::FragmentReassembly(std::size_t max_bytes) : max_bytes_(max_bytes) {}

bool FragmentReassembly::push(std::uint64_t message_id, Fragment fragment) {
  auto& entry = state_[message_id];
  if (entry.total_bytes + fragment.data.size() > max_bytes_) {
    return false;
  }
  entry.total_bytes += fragment.data.size();
  entry.has_last = entry.has_last || fragment.last;
  entry.fragments.push_back(std::move(fragment));
  return true;
}

std::optional<std::vector<std::uint8_t>> FragmentReassembly::try_reassemble(
    std::uint64_t message_id) {
  auto it = state_.find(message_id);
  if (it == state_.end()) {
    return std::nullopt;
  }
  auto& entry = it->second;
  if (!entry.has_last) {
    return std::nullopt;
  }

  std::sort(entry.fragments.begin(), entry.fragments.end(),
            [](const Fragment& a, const Fragment& b) { return a.offset < b.offset; });

  std::size_t assembled = 0;
  std::size_t expected_offset = 0;
  for (const auto& frag : entry.fragments) {
    if (frag.offset != expected_offset) {
      return std::nullopt;
    }
    assembled += frag.data.size();
    expected_offset += frag.data.size();
  }

  std::vector<std::uint8_t> output;
  output.reserve(assembled);
  for (const auto& frag : entry.fragments) {
    output.insert(output.end(), frag.data.begin(), frag.data.end());
  }
  state_.erase(it);
  return output;
}

}  // namespace veil::mux
