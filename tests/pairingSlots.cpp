// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#include <assert.h>
#include <vector>
#include "boards/PairingSlots.h"

int main() {
  using ed::detail::PairingRead;
  for (int newest = 0; newest < 2; ++newest) {
    uint32_t generations[2] = {3, 3};
    generations[newest] = 4;
    std::vector<int> attempts;
    const int loaded = ed::detail::loadPairingSlot(generations, [&](int slot) {
      attempts.push_back(slot);
      return slot != newest ? PairingRead::Loaded : PairingRead::Invalid;
    });
    assert(loaded == 1 - newest);
    assert((attempts == std::vector<int>{newest, 1 - newest}));
    attempts.clear();
    assert(ed::detail::loadPairingSlot(generations, [&](int slot) {
      attempts.push_back(slot);
      return PairingRead::Loaded;
    }) == newest);
    assert(attempts.size() == 1);
    attempts.clear();
    assert(ed::detail::loadPairingSlot(generations, [&](int slot) {
      attempts.push_back(slot);
      return slot == newest ? PairingRead::Unavailable : PairingRead::Loaded;
    }) == ed::detail::PAIRING_UNAVAILABLE);
    // Even a readable older identity must not be selected after an OOM failure.
    assert((attempts == std::vector<int>{newest}));
    assert(ed::detail::loadPairingSlot(generations, [](int) { return PairingRead::Invalid; }) == -1);
  }
  uint32_t generations[2] = {0, 2};
  int attempts = 0;
  assert(ed::detail::loadPairingSlot(generations, [&](int slot) {
    ++attempts;
    assert(slot == 1);
    return PairingRead::Loaded;
  }) == 1);
  assert(attempts == 1);
  generations[1] = 0;
  assert(ed::detail::loadPairingSlot(generations, [](int) { assert(false); return PairingRead::Loaded; }) == -1);
}
