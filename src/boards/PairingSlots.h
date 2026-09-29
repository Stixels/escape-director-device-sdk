// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#pragma once
#include <stdint.h>

namespace ed {
namespace detail {

enum class PairingRead { Loaded, Invalid, Unavailable };
constexpr int NO_PAIRING = -1, PAIRING_UNAVAILABLE = -2;

// Generation zero means an absent/invalid record. Try the newest record first,
// then the older one only for invalid data. Resource failures must not silently
// switch controller identity. Neither path needs two JSON documents in RAM.
template <typename Load> int loadPairingSlot(const uint32_t generations[2], Load load) {
  const int newest = generations[1] > generations[0] ? 1 : 0;
  for (int attempt = 0; attempt < 2; ++attempt) {
    const int slot = attempt ? 1 - newest : newest;
    if (!generations[slot])
      continue;
    const PairingRead result = load(slot);
    if (result == PairingRead::Loaded)
      return slot;
    if (result == PairingRead::Unavailable)
      return PAIRING_UNAVAILABLE;
  }
  return NO_PAIRING;
}

} // namespace detail
} // namespace ed
