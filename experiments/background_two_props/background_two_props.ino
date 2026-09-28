// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#include <Arduino.h>
#define ED_BACKGROUND_BENCH
#include <ExperimentalBackground.h>
#include "TwoProps.h"
#include "Description.h"

// Wiring. Buttons connect each input to GND; the sketch enables pull-ups.
constexpr uint8_t TAPS_INPUT = 3, HOLD_INPUT = 2;
#if defined(ARDUINO_UNOR4_WIFI)
#include <Arduino_LED_Matrix.h>
ArduinoLEDMatrix matrix;
#else
// GIGA uses the built-in RGB LED: held input blue, three taps red.
#endif

example::TwoProps props;
void showOutputs() {
  const bool held = props.hold.output(), tapped = props.taps.output();
  static int previous = -1;
  const int current = (held ? 1 : 0) | (tapped ? 2 : 0);
  if (current == previous) return;
  previous = current;
#if defined(ARDUINO_UNOR4_WIFI)
  // Two blocks with a dark gutter: left = D2 held, right = D3 three taps.
  uint8_t frame[8][12] = {};
  for (uint8_t row = 1; row < 7; ++row)
    for (uint8_t column = 0; column < 12; ++column)
      frame[row][column] = column < 5 ? held : column > 6 ? tapped : false;
  matrix.renderBitmap(frame, 8, 12);
#else
  digitalWrite(LEDB, held ? LOW : HIGH);
  digitalWrite(LEDR, tapped ? LOW : HIGH);
#endif
}

void completed(const char *id);

// Ordinary sketch logic: no timer or interrupt context.
void sample() {
  uint32_t now = millis();
  if (props.taps.sample(digitalRead(TAPS_INPUT) == LOW, now))
    completed(example::TAPS_ID);
  if (props.hold.sample(digitalRead(HOLD_INPUT) == LOW, now))
    completed(example::HOLD_ID);
  showOutputs();
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
#if defined(ARDUINO_UNOR4_WIFI)
  if (!matrix.begin()) Serial.println("LED matrix initialization failed");
#else
  pinMode(LEDB, OUTPUT);
  pinMode(LEDR, OUTPUT);
  pinMode(LEDG, OUTPUT);
  digitalWrite(LEDG, HIGH);
#endif
  showOutputs();
  if (!connection.begin(DESCRIPTION))
    Serial.println(
        "SDK worker unavailable: local puzzle only; offline in Escape Director");
}
void loop() {
  connection.poll();
  sample();
}
