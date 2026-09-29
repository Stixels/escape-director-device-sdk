// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#pragma once
#include <stdint.h>

namespace ed {

// Fast recovery after a stable connection; bounded backoff during an outage.
// All times come from the caller's monotonic millisecond clock (e.g. millis()).
class NetworkRetry {
public:
  bool ready(uint32_t nowMs) const {
    // Signed subtraction remains valid when the millisecond counter wraps.
    return !waiting || int32_t(nowMs - retryDeadlineMs) >= 0;
  }

  void attempted(uint32_t nowMs) {
    waiting = true;
    waitMs = retryDelayMs;
    retryDeadlineMs = nowMs + waitMs;
    retryDelayMs = retryDelayMs < MAX_DELAY_MS ? retryDelayMs * 2 : MAX_DELAY_MS;
  }

  // An attempt can block for seconds; the wait counts from when it ended so
  // the loop gets that time back between attempts.
  void finished(uint32_t nowMs) {
    if (waiting)
      retryDeadlineMs = nowMs + waitMs;
  }

  void observe(bool connected, uint32_t nowMs) {
    if (connected) {
      if (!wasConnected) {
        connectedSinceMs = nowMs;
      }
      // A brief reconnection must not continually reset outage backoff.
      if (uint32_t(nowMs - connectedSinceMs) >= STABLE_CONNECTION_MS) {
        retryDelayMs = INITIAL_DELAY_MS;
        waiting = false;
      }
    } else if (wasConnected) {
      waiting = true;
      retryDeadlineMs = nowMs + retryDelayMs;
    }
    wasConnected = connected;
  }

  void reset() {
    waiting = false;
    retryDelayMs = INITIAL_DELAY_MS;
    wasConnected = false;
  }

private:
  static constexpr uint32_t INITIAL_DELAY_MS = 1000;
  // A failed station connection blocks the loop for the modem's full timeout on
  // some boards, so long outages retry at most every 20 s.
  static constexpr uint32_t MAX_DELAY_MS = 20000;
  static constexpr uint32_t STABLE_CONNECTION_MS = 10000;
  uint32_t retryDeadlineMs = 0;
  uint32_t retryDelayMs = INITIAL_DELAY_MS;
  uint32_t waitMs = INITIAL_DELAY_MS;
  uint32_t connectedSinceMs = 0;
  bool waiting = false;
  bool wasConnected = false;
};

} // namespace ed
