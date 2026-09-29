// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT
//
// A four-button code: pressing the buttons on pins 2–5 in the order 1-3-2-4
// solves the puzzle, and a lock (the built-in LED here) releases two seconds
// later. Escape Director sees the progress, can release the lock or reset the
// code, and Test mode can pulse the lock. Each button connects its pin to GND.
#include <EscapeDirectorRoom.h>

constexpr uint8_t LOCK = LED_BUILTIN; // your lock relay pin in a real prop

ed::Room room("Button code", "1.0.0");
ed::Prop &code = room.prop("button-code", "Button code");
ed::Input button1(2), button2(3), button3(4), button4(5);

ed::Input *const answer[] = {&button1, &button3, &button2, &button4};
constexpr int CODE_LENGTH = 4;
int progress = 0; // correct presses so far
bool released = false;

void releaseLock() {
  digitalWrite(LOCK, HIGH);
  released = true;
}
void resetCode() {
  room.cancel(releaseLock);
  digitalWrite(LOCK, LOW);
  progress = 0;
  released = false;
}
void solve() {
  progress = CODE_LENGTH;
  room.after(2000, releaseLock); // a short pause before the lock opens
}

void setup() {
  pinMode(LOCK, OUTPUT);
  resetCode();

  code.signal("solved", "Code entered");
  code.command("release", "Release lock", solve);
  code.command("reset", "Reset code", resetCode);
  code.command("pulse", "Pulse lock").testPin(LOCK, HIGH, 1000);
  code.state("progress", "Progress", &progress, 0, CODE_LENGTH, "presses");
  code.state("released", "Lock released", &released);
  code.completion("solved", "release");
  room.onReset(resetCode);

  room.begin();
}

void loop() {
  room.loop();

  // More than 16 presses arrived while the board was busy: the order is
  // incomplete, so start the code again.
  if (room.lostPresses())
    progress = 0;

  // Presses in the order they happened, including any made while the board
  // was reconnecting.
  while (ed::Input *press = room.nextPress()) {
    if (progress == CODE_LENGTH)
      continue; // already solved
    if (press == answer[progress]) {
      if (++progress == CODE_LENGTH) {
        code.trigger("solved", *press);
        solve();
      }
    } else {
      progress = press == answer[0] ? 1 : 0;
    }
  }
}
