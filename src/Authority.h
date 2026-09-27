// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#pragma once
#include <stdint.h>

namespace ed {

// Tracks permission to run game or Test actions; it does not authenticate a peer.
// The authenticated adapter checks wire session IDs before calling this class.
// Single-threaded: board adapters must synchronize any interrupt-driven access.
class Authority {
public:
  enum class Mode { Idle, Game, Test };
  using TestCleanup = void (*)(void *);

  Authority(TestCleanup onTestEnded, void *context)
      : onTestEnded(onTestEnded), cleanupContext(context) {}

  // Capture this value when admitting work. A mode/session change invalidates it.
  uint32_t generation() const {
    return currentGeneration;
  }

  void disconnect() {
    online = false;
    ++currentGeneration;
    clearMode();
  }

  // Call only after authentication. Reconnecting never resumes a game or Test.
  void connected() {
    disconnect();
    online = true;
  }

  void tick(uint32_t nowMs) {
    // Signed subtraction handles the wrapping Arduino millisecond counter.
    if (currentMode != Mode::Idle && int32_t(nowMs - leaseDeadlineMs) >= 0) {
      ++currentGeneration;
      clearMode();
    }
  }

  bool enter(Mode mode, uint32_t leaseMs, uint32_t nowMs) {
    tick(nowMs);
    if (!online || currentMode != Mode::Idle || mode == Mode::Idle || !validLease(leaseMs)) {
      return false;
    }
    ++currentGeneration;
    currentMode = mode;
    leaseDeadlineMs = nowMs + leaseMs;
    return true;
  }

  bool renew(uint32_t generation, uint32_t leaseMs, uint32_t nowMs) {
    tick(nowMs);
    if (!online || currentMode == Mode::Idle || generation != currentGeneration ||
        !validLease(leaseMs)) {
      return false;
    }
    leaseDeadlineMs = nowMs + leaseMs;
    return true;
  }

  bool leave(uint32_t generation, uint32_t nowMs) {
    tick(nowMs);
    if (!online || generation != currentGeneration || currentMode == Mode::Idle) {
      return false;
    }
    ++currentGeneration;
    clearMode();
    return true;
  }

  bool allows(Mode mode, uint32_t generation, uint32_t nowMs) {
    tick(nowMs);
    return online && mode != Mode::Idle && currentMode == mode && currentGeneration == generation;
  }

private:
  static constexpr uint32_t MAX_LEASE_MS = 3000;
  TestCleanup onTestEnded;
  void *cleanupContext;
  uint32_t currentGeneration = 0;
  uint32_t leaseDeadlineMs = 0;
  bool online = false;
  Mode currentMode = Mode::Idle;

  bool validLease(uint32_t durationMs) const {
    return durationMs > 0 && durationMs <= MAX_LEASE_MS;
  }

  void clearMode() {
    const bool wasTesting = currentMode == Mode::Test;
    currentMode = Mode::Idle;
    // Stop temporary Test effects, preserving ordinary local puzzle state.
    if (wasTesting && onTestEnded) {
      onTestEnded(cleanupContext);
    }
  }
};

} // namespace ed
