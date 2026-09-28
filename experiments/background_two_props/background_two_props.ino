// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#include <Arduino.h>
#define ED_BACKGROUND_BENCH
#include <ExperimentalBackground.h>
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
void setOutput(uint8_t pin, bool active) {
  digitalWrite(pin, active != OUTPUT_ACTIVE_LOW ? HIGH : LOW);
}

void completed(const char *id);

// Ordinary sketch logic: no timer or interrupt context.
void sample() {
  uint32_t now = millis();
  if (props.taps.sample(digitalRead(TAPS_INPUT) == LOW, now))
    completed(example::TAPS_ID);
  if (props.hold.sample(digitalRead(HOLD_INPUT) == LOW, now))
    completed(example::HOLD_ID);
  setOutput(TAPS_OUTPUT, props.taps.output());
  setOutput(HOLD_OUTPUT, props.hold.output());
}
class Driver : public ed::experimental::Driver {
  void state(JsonObject report) override {
    bool tapsSolved = props.taps.solved, tapsOutput = props.taps.output();
    bool holdSolved = props.hold.solved, holdOutput = props.hold.output();
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
    // Only the temporary pulse is testable. Complete/Reset change persistent
    // puzzle state and remain ordinary commands, not Test effects.
    if (testing && strcmp(capability, "pulse"))
      return false;
    if (!strcmp(capability, "reset"))
      prop->reset();
    else if (!strcmp(capability, "pulse"))
      prop->testPulse(millis());
    else if (prop->complete() && !testing) {
      completed(id);
    }
    return true;
  }
  void resetRoom() override {
    props.taps.reset();
    props.hold.reset();
  }
  void endTest() override {
    props.stopTest();
  }
} driver;
ed::experimental::BackgroundClient connection(driver);
void completed(const char *id) { connection.signal(id, "completed"); }

void setup() {
  pinMode(TAPS_INPUT, INPUT_PULLUP);
  pinMode(HOLD_INPUT, INPUT_PULLUP);
  pinMode(TAPS_OUTPUT, OUTPUT);
  pinMode(HOLD_OUTPUT, OUTPUT);
  setOutput(TAPS_OUTPUT, false);
  setOutput(HOLD_OUTPUT, false);
  if (!connection.begin(DESCRIPTION))
    Serial.println(
        "SDK worker unavailable: local puzzle only; offline in Escape Director");
}
void loop() {
  connection.poll();
  sample();
}
