// Copyright 2026 Stixels
// SPDX-License-Identifier: Apache-2.0

#include <Arduino.h>
#include <EscapeDirector.h>
#include "TwoProps.h"
#include "Description.h"

// Wiring. Buttons connect each input to GND; the sketch enables pull-ups.
constexpr uint8_t TAPS_INPUT = 2, HOLD_INPUT = 3;
#if defined(ARDUINO_GIGA)
// Built-in blue and red LEDs, lit by a LOW output.
constexpr uint8_t TAPS_OUTPUT = LEDB, HOLD_OUTPUT = LEDR;
constexpr bool OUTPUT_ACTIVE_LOW = true;
#else
// Built-in LED (D13). Wire an LED and resistor from D12 to GND to see Hold button.
constexpr uint8_t TAPS_OUTPUT = LED_BUILTIN, HOLD_OUTPUT = 12;
constexpr bool OUTPUT_ACTIVE_LOW = false;
#endif

example::TwoProps props;
volatile uint32_t testDeadline = 0;
struct Edge {
  uint32_t game = 0, at = 0;
};
Edge tapsEdge, holdEdge;

void setOutput(uint8_t pin, bool active) {
  digitalWrite(pin, active != OUTPUT_ACTIVE_LOW ? HIGH : LOW);
}

// The SDK timer owns sampling and physical deadlines, independent of Wi-Fi/TLS.
void sample() {
  uint32_t now = millis();
  if (testDeadline && int32_t(now - testDeadline) >= 0) {
    props.stopTest();
    testDeadline = 0;
  }
  if (props.taps.sample(digitalRead(TAPS_INPUT) == LOW, now))
    tapsEdge = {ed::captureGame(), now};
  if (props.hold.sample(digitalRead(HOLD_INPUT) == LOW, now))
    holdEdge = {ed::captureGame(), now};
  setOutput(TAPS_OUTPUT, props.taps.output());
  setOutput(HOLD_OUTPUT, props.hold.output());
}
class Driver : public ed::CustomDriver {
  void state(JsonObject report) override {
    noInterrupts();
    bool tapsSolved = props.taps.solved, tapsOutput = props.taps.output();
    bool holdSolved = props.hold.solved, holdOutput = props.hold.output();
    interrupts();
    auto values = report["props"].to<JsonArray>();
    auto taps = values.add<JsonObject>();
    taps["id"] = example::TAPS_ID;
    taps["values"]["solved"] = tapsSolved;
    taps["values"]["output"] = tapsOutput;
    auto hold = values.add<JsonObject>();
    hold["id"] = example::HOLD_ID;
    hold["values"]["solved"] = holdSolved;
    hold["values"]["output"] = holdOutput;
  }
  bool command(const char *id, const char *capability, bool testing) override {
    example::Prop *prop = !strcmp(id, example::TAPS_ID)   ? &props.taps
                          : !strcmp(id, example::HOLD_ID) ? &props.hold
                                                          : nullptr;
    if (!prop || (strcmp(capability, "reset") && strcmp(capability, "pulse") &&
                  strcmp(capability, "complete")))
      return false;
    noInterrupts();
    if (!strcmp(capability, "reset"))
      prop->reset();
    else if (!strcmp(capability, "pulse"))
      prop->testPulse(millis());
    else if (prop->complete() && !testing) {
      // Use the same one-shot signal path as a physical solve. Test mode
      // changes local state but cannot start Room Automations.
      Edge &edge = prop == &props.taps ? tapsEdge : holdEdge;
      edge = {ed::captureGame(), millis()};
    }
    interrupts();
    return true;
  }
  void resetRoom() override {
    noInterrupts();
    props.taps.reset();
    props.hold.reset();
    interrupts();
  }
  void endTest() override {
    noInterrupts();
    props.stopTest();
    testDeadline = 0;
    interrupts();
  }
  void testLease(uint32_t deadline) override {
    testDeadline = deadline;
  }
} driver;
void setup() {
  pinMode(TAPS_INPUT, INPUT_PULLUP);
  pinMode(HOLD_INPUT, INPUT_PULLUP);
  pinMode(TAPS_OUTPUT, OUTPUT);
  pinMode(HOLD_OUTPUT, OUTPUT);
  setOutput(TAPS_OUTPUT, false);
  setOutput(HOLD_OUTPUT, false);
  ed::begin(DESCRIPTION, driver);
  ed::startTimer(sample, 5);
}
void loop() {
  ed::poll();
  noInterrupts();
  Edge taps = tapsEdge, hold = holdEdge;
  tapsEdge = {};
  holdEdge = {};
  interrupts();
  if (taps.game)
    ed::signal(example::TAPS_ID, "completed", taps.game, taps.at);
  if (hold.game)
    ed::signal(example::HOLD_ID, "completed", hold.game, hold.at);
}
