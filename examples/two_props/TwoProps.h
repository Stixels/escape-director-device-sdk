// Copyright 2026 Stixels
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stdint.h>

namespace example {

// User-owned puzzle logic; independent of Arduino, networking and managed templates.
constexpr const char *TAPS_ID = "0210a18f-c31f-45b7-a35a-e20eb82a6c11";
constexpr const char *HOLD_ID = "91ff0128-8086-473f-8187-fd36ff920b04";
enum class Pattern { ThreePresses, WhilePressed };

struct Prop {
  static constexpr uint32_t DEBOUNCE_MS = 25;
  static constexpr uint32_t TEST_PULSE_MS = 1000;
  static constexpr uint8_t REQUIRED_PRESSES = 3;

  Pattern pattern = Pattern::ThreePresses;
  bool forcedOutput = false;
  bool raw = false;
  bool pressed = false;
  bool solved = false;
  bool temporaryOutput = false;
  uint8_t taps = 0;
  uint32_t changedAt = 0;
  uint32_t testUntil = 0;

  // Momentary inputs emit once per debounced press; latched props once per solve.
  bool sample(bool inputActive, uint32_t nowMs) {
    if (temporaryOutput && int32_t(nowMs - testUntil) >= 0) {
      temporaryOutput = false;
    }
    if (raw != inputActive) {
      raw = inputActive;
      changedAt = nowMs;
    }

    const bool wasSolved = solved;
    const bool wasPressed = pressed;
    if (pressed != raw && uint32_t(nowMs - changedAt) >= DEBOUNCE_MS) {
      pressed = raw;
      if (pressed) {
        if (taps < REQUIRED_PRESSES) {
          ++taps;
        }
      }
    }

    const bool completed = pattern == Pattern::WhilePressed
                               ? pressed
                               : taps >= REQUIRED_PRESSES;
    if (completed) {
      solved = true;
    }
    // Release rearms the physical signal without undoing recorded completion.
    return pattern == Pattern::WhilePressed ? !wasPressed && pressed
                                            : !wasSolved && solved;
  }

  // Repeated completion commands do not emit another completion signal.
  bool complete() {
    const bool changed = !solved;
    solved = true;
    forcedOutput = true;
    return changed;
  }

  bool output() const {
    // Completion is persistent; a momentary physical output follows the button.
    // Explicit Complete prop commands override it until Reset.
    return temporaryOutput || forcedOutput ||
           (pattern == Pattern::WhilePressed ? pressed : solved);
  }

  void reset() {
    solved = false;
    taps = 0;
    forcedOutput = false;
    temporaryOutput = false;
  }

  void testPulse(uint32_t nowMs) {
    temporaryOutput = true;
    testUntil = nowMs + TEST_PULSE_MS;
  }
};

struct TwoProps {
  Prop taps;
  Prop hold{Pattern::WhilePressed};

  void stopTest() {
    taps.temporaryOutput = false;
    hold.temporaryOutput = false;
  }

  // Adapts this instance to the SDK's callback plus context-pointer interface.
  static void cleanup(void *context) {
    static_cast<TwoProps *>(context)->stopTest();
  }
};

} // namespace example
