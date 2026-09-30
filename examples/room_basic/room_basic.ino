// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT
//
// A train door: pulling the lever opens it for five seconds and reports
// "solved". Escape Director can open or close the door; in Test mode, Open door
// opens it for one second. The built-in LED stands in for the door relay, and
// the lever is a button (or a jumper wire) from pin 2 to GND, so nothing else
// needs wiring.
#include <EscapeDirectorRoom.h>

constexpr uint8_t DOOR_RELAY = LED_BUILTIN; // your relay pin in a real prop
// The level that opens the door. Many relay modules, and the GIGA's built-in
// LED, switch on with LOW; match your wiring.
#if defined(ARDUINO_GIGA)
constexpr uint8_t DOOR_OPEN = LOW;
#else
constexpr uint8_t DOOR_OPEN = HIGH;
#endif
constexpr uint8_t DOOR_CLOSED = DOOR_OPEN == HIGH ? LOW : HIGH;

ed::Room room("Train door", "1.0.0");
ed::Prop &door = room.prop("door", "Train door");
ed::Input lever(2); // sampled by the SDK, so a pull during a reconnect is kept

bool doorOpen = false;

void doorClosed() { doorOpen = false; }
void openDoor() {
  // Five seconds (to within the SDK's 5 ms timer), even while the board is
  // reconnecting.
  room.pulse(DOOR_RELAY, DOOR_OPEN, 5000, doorClosed);
  doorOpen = true;
}
void closeDoor() {
  digitalWrite(DOOR_RELAY, DOOR_CLOSED);
  doorOpen = false;
}

void setup() {
  pinMode(DOOR_RELAY, OUTPUT);
  closeDoor();

  door.signal("solved", "Lever pulled");
  door.command("open", "Open door", openDoor).testPin(DOOR_RELAY, DOOR_OPEN, 1000);
  door.command("reset", "Close door", closeDoor);
  door.state("open", "Open", &doorOpen);
  door.completion("solved", "open");
  room.onReset(closeDoor); // pulses end automatically on Room reset

  room.begin();
}

void loop() {
  room.loop();
  if (lever.pressed()) {
    openDoor();
    door.trigger("solved", lever); // with the press's time and Game
  }
}
