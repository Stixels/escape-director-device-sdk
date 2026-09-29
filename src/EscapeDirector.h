// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include "BoardAdapter.h"

// Internal core under ed::Room. Sketches include <EscapeDirectorRoom.h>; the
// declarations here are the SDK's plumbing, not a supported sketch API.

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
  // Internal dispatch deadline for asynchronous adapters; legacy drivers need no change.
  virtual void commandDeadline(uint32_t deadlineMs) { (void)deadlineMs; }
  virtual ~CustomDriver() = default;
};

// Supplies the firmware description without keeping it in RAM. ed::Room
// generates it from its declarations.
class DescriptionSource {
public:
  virtual size_t descriptionLength() = 0;       // compact JSON bytes
  virtual void writeDescription(Print &out) = 0; // compact JSON
  virtual const char *firmwareVersion() = 0;
  // Whether propId declares capability as a signal (command == false) or as a
  // command; testing additionally requires a testable command.
  virtual bool declares(const char *propId, const char *capability, bool command, bool testing) = 0;
  virtual ~DescriptionSource() = default;
};

// One client per sketch. Call begin once; keep the description, driver and
// adapter alive for the entire sketch.
void begin(DescriptionSource &description, CustomDriver &driver, BoardAdapter &adapter);

// Boards with a bundled adapter.
#if defined(ARDUINO_GIGA) || defined(ARDUINO_UNOR4_WIFI)
#define ED_BUNDLED_BOARD 1
namespace detail {
BoardAdapter &defaultBoard();
}
#endif

void poll();

// Call on a local completion edge. Returns zero outside live Game authority.
uint32_t captureGame();
bool signal(const char *propId, const char *capability, uint32_t capturedGame,
            uint32_t capturedAtMs);

} // namespace ed
