// Copyright 2026 Stixels
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stddef.h>

namespace ed {

// Try addresses reachable on the local subnet before routed/VPN candidates.
// Adapters supply the subnet check; every candidate remains in the retry cycle.
template <typename IsLocal>
size_t stationAddressIndex(size_t attempt, size_t count, IsLocal isLocal) {
  if (!count)
    return 0;
  size_t remaining = attempt % count;
  for (int pass = 0; pass < 2; ++pass) {
    for (size_t index = 0; index < count; ++index) {
      if (isLocal(index) != (pass == 0))
        continue;
      if (remaining == 0)
        return index;
      --remaining;
    }
  }
  return 0;
}

} // namespace ed
