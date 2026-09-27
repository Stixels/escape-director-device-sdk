// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#pragma once
#include <stdint.h>

namespace ed {

// Estimate station time from a monotonic board clock. Each reply must match a
// recent request: a delayed MQTT reply must never make an old command fresh.
class StationClock {
public:
  void begin(int64_t stationMs, uint32_t localMs) {
    anchor = stationMs;
    anchorAt = localMs;
    nextRequestAt = localMs;
    pending = false;
  }

  int64_t now(uint32_t localMs) const {
    return anchor + uint32_t(localMs - anchorAt);
  }

  // Call only while authenticated. Zero means no request is due.
  uint32_t request(uint32_t localMs) {
    if (int32_t(localMs - nextRequestAt) < 0)
      return 0;
    if (++sequence == 0)
      ++sequence;
    sentAt = localMs;
    nextRequestAt = localMs + 10000;
    pending = true;
    return sequence;
  }

  bool receive(uint32_t replySequence, int64_t stationMs, uint32_t localMs) {
    if (!pending || replySequence != sequence || stationMs <= 0)
      return false;
    pending = false;
    const uint32_t roundTrip = localMs - sentAt;
    if (roundTrip > 500)
      return false;
    // Half the bounded round trip estimates the reply's travel time.
    anchor = stationMs + roundTrip / 2;
    anchorAt = localMs;
    return true;
  }

private:
  int64_t anchor = 0;
  uint32_t anchorAt = 0, nextRequestAt = 0, sentAt = 0, sequence = 0;
  bool pending = false;
};

} // namespace ed
