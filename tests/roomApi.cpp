// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT
//
// Runs the real ed::Room, ed::Input and room.pulse() code on the host against
// tests/host stand-ins: a fake clock, fake pins, and an SDK core whose poll()
// can block like a reconnect while the timer keeps running.
#include <cassert>
#include <string>
#include <vector>

#include "../src/EscapeDirectorRoom.cpp"
#include "../src/RoomRegistry.h"

namespace {
void (*timerTick)() = nullptr;
uint32_t game = 0;
// The next poll() blocks: it runs whilePolling (which advances time), then
// waits blockMs more.
void (*whilePolling)() = nullptr;
uint32_t blockMs = 0;
struct Sent {
  std::string prop, capability;
  uint32_t game, atMs;
};
std::vector<Sent> sent;

// The timer keeps firing every 5 ms while time passes, as on the board.
void advance(uint32_t ms) {
  for (uint32_t i = 0; i < ms; ++i) {
    ++fake::now;
    if (fake::now % 5 == 0 && timerTick)
      timerTick();
  }
}

class FakeBoard : public ed::BoardAdapter {
public:
  const char *id() const override { return "host"; }
  void begin() override {}
  Stream &setupStream() override { return Serial; }
  Client &secureClient() override { return client; }
  void trustStation(const char *) override {}
  bool networkConnected() override { return true; }
  bool joinNetwork(const char *, const char *) override { return true; }
  void disconnectNetwork() override {}
  bool scanNetworks(JsonArray) override { return true; }
  IPAddress localIP() override { return {}; }
  IPAddress subnetMask() override { return {}; }
  UDP &discoverySocket() override { return udp; }
  bool loadPairing(JsonDocument &) override { return false; }
  bool savePairing(JsonVariantConst) override { return false; }
  bool startTimer(void (*tick)(), uint32_t periodMs) override {
    assert(periodMs == 5);
    timerTick = tick;
    return true;
  }

private:
  Client client;
  UDP udp;
};
} // namespace

// The SDK core, reduced to what ed::Room calls.
namespace ed {
namespace detail {
BoardAdapter &defaultBoard() {
  static FakeBoard board;
  return board;
}
} // namespace detail
void begin(const char *, CustomDriver &, BoardAdapter &) {}
void begin(DescriptionSource &, CustomDriver &, BoardAdapter &) {}
void poll() {
  if (void (*during)() = whilePolling) {
    whilePolling = nullptr;
    during();
  }
  const uint32_t ms = blockMs;
  blockMs = 0;
  advance(ms);
}
uint32_t captureGame() { return game; }
// The core's stale-event rule: same Game, at most 2 s old.
bool signal(const char *propId, const char *capability, uint32_t capturedGame,
            uint32_t capturedAtMs) {
  if (!capturedGame || capturedGame != game || millis() - capturedAtMs > 2000)
    return false;
  sent.push_back({propId, capability, capturedGame, capturedAtMs});
  return true;
}
} // namespace ed

namespace {
constexpr uint8_t BUTTON = 2, SWITCH = 3, STICK_LEFT = 4, STICK_RIGHT = 5, DOOR = 12, LAMP = 13;

ed::Input button(BUTTON), reed(SWITCH), left(STICK_LEFT), right(STICK_RIGHT);

int doorClosedCalls = 0, resets = 0;
uint32_t openedAt = 0;
void doorClosed() { ++doorClosedCalls; }
void nothing() {}
void onReset() { ++resets; }

// A press: the pin goes LOW (INPUT_PULLUP wiring) for 30 ms, then HIGH.
void press(uint8_t pin) {
  fake::levels[pin] = LOW;
  advance(30);
  fake::levels[pin] = HIGH;
  advance(30);
}

void loopPass(ed::Room &room) {
  room.loop();
  assert(fake::masked == 0);
}
} // namespace

int main() {
  for (uint8_t pin : {BUTTON, SWITCH, STICK_LEFT, STICK_RIGHT})
    fake::levels[pin] = HIGH;
  fake::now = 1000;

  // Invalid declarations do not crash while chaining; begin() reports them.
  {
    ed::Room broken("Broken", "1.0.0");
    ed::Prop &bad = broken.prop("Not A Slug", "Bad");
    bad.command("open", "Open", nothing).testPin(LAMP, HIGH, 100).test(nothing, nothing);
    bad.signal("solved", "Solved").state("on", "On", static_cast<const bool *>(nullptr));
    ed::Prop &good = broken.prop("door", "Door");
    good.command("Bad ID", "Bad").testPin(LAMP, HIGH).test(nullptr, nothing);
    assert(!broken.begin());
    assert(Serial.output.find("Escape Director: ") != std::string::npos);
  }

  ed::Room room("Host room", "1.0.0");
  ed::Prop &door = room.prop("door", "Door");
  door.signal("solved", "Solved");
  door.command("flash", "Flash lamp").testPin(LAMP, HIGH, 1000);
  room.onReset(onReset);
  assert(room.begin());
  assert(fake::modes[BUTTON] == INPUT_PULLUP && timerTick);
  char doorId[37];
  ed::room::derivedUuid("door", doorId);

  // A press during a live Game is reported with its own time and Game.
  game = 7;
  const uint32_t pressedAt = fake::now;
  press(BUTTON);
  loopPass(room);
  assert(button.pressed());
  assert(door.trigger("solved", button));
  assert(sent.size() == 1 && sent[0].game == 7);
  assert(sent[0].atMs >= pressedAt + 20 && sent[0].atMs <= pressedAt + 30);
  assert(sent[0].prop == doorId && sent[0].capability == "solved");
  assert(!button.pressed());

  // A press made during a long pause still drives local logic afterwards, but
  // is not reported as new: its Game ended and it is older than 2 s.
  press(BUTTON);
  game = 8;       // the station started another Game meanwhile
  blockMs = 10000; // then a reconnect blocked room.loop() for 10 s
  loopPass(room);
  assert(button.pressed());
  assert(!door.trigger("solved", button));
  game = 7;
  assert(!door.trigger("solved", button)); // same Game, but 10 s old
  assert(sent.size() == 1);
  game = 8;

  // Presses nobody asks for are dropped after one loop() pass, so a switch read
  // only with active() cannot fill the queue or block buttons.
  press(SWITCH);
  loopPass(room); // visible to the sketch from here
  loopPass(room); // the pass never asked
  assert(!room.nextPress());
  // pressed() once per pass keeps later presses of that input.
  whilePolling = [] { // two presses while room.loop() blocks
    press(BUTTON);
    press(BUTTON);
  };
  loopPass(room);
  assert(button.pressed());
  loopPass(room);
  assert(button.pressed());
  loopPass(room);
  assert(!button.pressed());

  // Sequences: order is kept, and overflow is reported instead of reordering.
  // 20 presses cycling over three inputs during one pause: the first 16 are
  // kept in order, the last 4 are counted lost.
  static ed::Input *const cycle[] = {&left, &right, &reed};
  static const uint8_t cyclePins[] = {STICK_LEFT, STICK_RIGHT, SWITCH};
  whilePolling = [] {
    for (int i = 0; i < 20; ++i)
      press(cyclePins[i % 3]);
  };
  loopPass(room);
  int taken = 0;
  while (ed::Input *next = room.nextPress()) {
    assert(next == cycle[taken % 3]);
    ++taken;
  }
  assert(taken == 16);
  assert(room.lostPresses() == 4);
  assert(room.lostPresses() == 0);
  // A pass that drains with nextPress() one press at a time keeps the rest.
  whilePolling = [] {
    press(STICK_LEFT);
    press(STICK_RIGHT);
  };
  loopPass(room);
  assert(room.nextPress() == &left);
  loopPass(room);
  assert(room.nextPress() == &right);
  assert(!room.nextPress());

  // pulse(): the timer ends it on time even while room.loop() blocks.
  pinMode(DOOR, OUTPUT);
  fake::levels[DOOR] = LOW;
  openedAt = fake::now;
  assert(room.pulse(DOOR, HIGH, 5000, doorClosed));
  assert(fake::levels[DOOR] == HIGH);
  advance(4000);
  whilePolling = [] { // the pulse ends while room.loop() blocks
    advance(openedAt + 4999 - fake::now);
    assert(fake::levels[DOOR] == HIGH);
    advance(5); // the next 5 ms timer tick
    assert(fake::levels[DOOR] == LOW);
    assert(doorClosedCalls == 0); // done runs from room.loop(), not the timer
  };
  blockMs = 3000;
  loopPass(room);
  assert(fake::levels[DOOR] == LOW && doorClosedCalls == 1);
  // Restarting a pulse that ended but whose done has not run yet cancels that
  // done: it would otherwise report the new pulse as ended.
  assert(room.pulse(DOOR, HIGH, 100, doorClosed));
  advance(110);
  assert(fake::levels[DOOR] == LOW);
  assert(room.pulse(DOOR, HIGH, 100, doorClosed));
  loopPass(room);
  assert(doorClosedCalls == 1 && fake::levels[DOOR] == HIGH);
  advance(110);
  loopPass(room);
  assert(doorClosedCalls == 2);
  // Room reset ends pulses without done and clears waiting presses.
  assert(room.pulse(DOOR, HIGH, 5000, doorClosed));
  press(BUTTON);
  room.resetRoom();
  assert(fake::levels[DOOR] == LOW && resets == 1);
  loopPass(room);
  assert(doorClosedCalls == 2 && !room.nextPress());

  // Test pins end at their duration, or earlier when the Test lease expires.
  assert(room.command(doorId, "flash", true));
  assert(fake::levels[LAMP] == HIGH);
  advance(1005);
  assert(fake::levels[LAMP] == LOW);
  room.testLease(fake::now + 300);
  assert(room.command(doorId, "flash", true));
  advance(305);
  assert(fake::levels[LAMP] == LOW);
  assert(!room.command(doorId, "flash", false)); // Test-only command

  std::puts("roomApi passed");
  return 0;
}
