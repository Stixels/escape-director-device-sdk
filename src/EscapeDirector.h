// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include "BoardAdapter.h"

namespace ed {

// Callbacks run on the Arduino loop thread. Synchronize shared I/O state if a
// timer services it. Commands schedule work and return without blocking.
class CustomDriver {
public:
  virtual void state(JsonObject report) = 0;
  virtual bool command(const char *propId, const char *capability, bool testing) = 0;
  virtual void resetRoom() = 0;
  virtual void endTest() = 0;
  // A board timer must enforce this monotonic deadline even while Wi-Fi blocks.
  virtual void testLease(uint32_t deadlineMs) = 0;
  virtual ~CustomDriver() = default;
};

// One client per sketch. Call begin once; keep the driver and adapter alive for the entire sketch.
// descriptionJson must stay valid for the whole sketch; a constexpr array stays
// in flash and is streamed when sent, so its size does not consume RAM.
void begin(const char *descriptionJson, CustomDriver &driver, BoardAdapter &adapter);

// Boards with a bundled adapter. Other boards pass their own adapter to begin.
#if defined(ARDUINO_GIGA) || defined(ARDUINO_UNOR4_WIFI)
namespace detail {
BoardAdapter &defaultBoard();
}
inline void begin(const char *descriptionJson, CustomDriver &driver) {
  begin(descriptionJson, driver, detail::defaultBoard());
}
#endif

void poll();

// Runs tick from a board timer every periodMs, even while networking blocks.
// Sample inputs and enforce Test deadlines here. Call after begin, once.
bool startTimer(void (*tick)(), uint32_t periodMs);

// Call on a local completion edge. Returns zero outside live Game authority.
uint32_t captureGame();
bool signal(const char *propId, const char *capability, uint32_t capturedGame,
            uint32_t capturedAtMs);

} // namespace ed
