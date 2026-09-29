// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ed {

// Leading bits two IPv4 addresses share (0-32).
inline int commonPrefixBits(uint32_t a, uint32_t b) {
  uint32_t diff = a ^ b;
  int bits = 0;
  for (uint32_t mask = 0x80000000u; mask && !(diff & mask); mask >>= 1)
    ++bits;
  return bits;
}

// The attempt-th address to try, most promising first: the address sharing
// the longest prefix with the controller's own IP (its subnet, then the same
// private range across routed segments) before VPN and other routed
// candidates. Ties keep the station's order; every candidate stays in the
// cycle. prefixBits(index) scores a candidate; negative means unusable.
template <typename PrefixBits>
size_t stationAddressIndex(size_t attempt, size_t count, PrefixBits prefixBits) {
  if (!count)
    return 0;
  if (count > 32)
    count = 32;
  const size_t rank = attempt % count;
  uint32_t used = 0;
  size_t chosen = 0;
  for (size_t r = 0; r <= rank; ++r) {
    int best = -2;
    for (size_t index = 0; index < count; ++index) {
      if (used & (1u << index))
        continue;
      const int score = prefixBits(index);
      if (score > best) {
        best = score;
        chosen = index;
      }
    }
    used |= 1u << chosen;
  }
  return chosen;
}

} // namespace ed
