#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace veil::mux {

struct Fragment {
  std::uint16_t offset{0};
  std::vector<std::uint8_t> data;
  bool last{false};
};

class FragmentReassembly {
 public:
  explicit FragmentReassembly(std::size_t max_bytes = 1 << 20);
  bool push(std::uint64_t message_id, Fragment fragment);
  std::optional<std::vector<std::uint8_t>> try_reassemble(std::uint64_t message_id);

 private:
  struct State {
    std::vector<Fragment> fragments;
    std::size_t total_bytes{0};
    bool has_last{false};
  };

  std::size_t max_bytes_;
  std::map<std::uint64_t, State> state_;
};

}  // namespace veil::mux
