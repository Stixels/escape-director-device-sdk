// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#pragma once
// Declarative integration API. Declare props, signals, commands and state
// beside the code that uses them; call room.begin() in setup() and room.loop()
// in loop(). The SDK generates the description, derives stable prop IDs, and
// owns networking, Game and Test rules. Every callback you pass runs from
// room.loop(), never from an interrupt.
#include "EscapeDirector.h"
#include "RoomRegistry.h"
#include <limits.h>

namespace ed {

class RoomBase;

// A button or switch sampled by the SDK's timer every 5 ms, so presses are not
// lost while room.loop() waits on the network. Declare it globally; room.begin()
// configures the pin. Reads happen in your ordinary loop() code.
//
// Each press is remembered with the time and Game in which it happened, until
// the loop() pass after room.loop() has returned it to the sketch: a press that
// no code asks for during that pass (pressed() or room.nextPress()) is dropped,
// so switches read only with active() never fill the queue.
class Input {
public:
  // mode: INPUT_PULLUP (active LOW, the usual wiring to GND) or INPUT.
  explicit Input(uint8_t pin, uint8_t mode = INPUT_PULLUP, bool activeLow = true,
                 uint16_t debounceMs = 20);
  // True once for each press, including presses made while the loop was busy.
  bool pressed();
  // The debounced current state.
  bool active() const { return stable; }

private:
  friend class RoomBase;
  friend class Prop;
  void sample(uint32_t now, uint32_t game);
  uint8_t pin, mode;
  bool activeLow;
  uint8_t debounceSamples;
  volatile bool stable = false;
  volatile uint8_t changing = 0;
  bool asked = false; // pressed() was called during this loop() pass
  // The press most recently returned by pressed() or room.nextPress().
  uint32_t pressAt = 0, pressGame = 0;
  Input *next = nullptr;
  static Input *first;
};

// Returned by Prop::command to add an optional Test behavior.
class Command {
public:
  Command(RoomBase *room, int entry) : room(room), entry(entry) {}
  // Test mode drives pin to activeLevel for durationMs (at most until Test
  // ends), switched off by the SDK's timer even while networking blocks.
  // Configure the pin as an OUTPUT in setup().
  Command &testPin(uint8_t pin, uint8_t activeLevel, uint32_t durationMs = 1000);
  // Test mode calls start; stop runs from room.loop() when Test ends, its lease
  // expires or the Room resets. Actions scheduled inside start end with Test.
  Command &test(room::Action start, room::Action stop);

private:
  RoomBase *room;
  int entry;
};

class Prop {
public:
  Prop() = default;
  Prop &signal(const char *id, const char *name);
  // A command without a handler is Test-only (add testPin or test).
  Command command(const char *id, const char *name, room::Action handler = nullptr);
  Prop &state(const char *id, const char *name, const bool *value);
  Prop &state(const char *id, const char *name, const int *value, int min, int max,
              const char *unit = nullptr);
#if LONG_MAX == INT32_MAX // every supported board; not 64-bit hosts
  Prop &state(const char *id, const char *name, const long *value, long min, long max,
              const char *unit = nullptr);
#endif
  // Enum state: *index selects one of values.
  template <size_t N>
  Prop &state(const char *id, const char *name, const uint8_t *index, const char *const (&values)[N]) {
    return enumState(id, name, index, values, N);
  }
  // Links this prop's completion signal and command for Puzzle completion.
  Prop &completion(const char *signal, const char *command);
  // Reports signal now. Call it once, at the transition (for example when a
  // puzzle becomes solved). False outside a live Game or while offline.
  bool trigger(const char *signal);
  // Reports signal as caused by cause's last press (from pressed() or
  // room.nextPress()), with that press's time and Game. A press that waited
  // through a reconnect still drives local logic, but is not reported as a new
  // event: the SDK drops signals from another Game or older than 2 s.
  bool trigger(const char *signal, const Input &cause);
  // Reports signal when condition changes from false to true, and returns true
  // at that change whether or not it could be reported, so puzzle logic can
  // act on the same edge.
  bool triggerWhen(const char *signal, bool condition);

private:
  friend class RoomBase;
  friend class Command;
  Prop &enumState(const char *id, const char *name, const uint8_t *index,
                  const char *const *values, size_t count);
  Prop &number(const char *id, const char *name, const int32_t *value, int32_t min,
               int32_t max, const char *unit);
  bool report(const char *signal, uint32_t game, uint32_t atMs);
  RoomBase *room = nullptr;
  uint8_t index = 0xff;
};

class RoomBase : public CustomDriver, public DescriptionSource {
public:
  // uuid keeps an existing Device's prop ID (and its links) when migrating.
  Prop &prop(const char *slug, const char *name, const char *uuid = nullptr);
  void onReset(room::Action handler) { resetHandler = handler; }
  // Runs action from room.loop() after ms. Scheduling an action that is already
  // pending replaces it. Room reset cancels every pending action.
  bool after(uint32_t ms, room::Action action);
  void cancel(room::Action action) { scheduler.cancel(action); }
  // Drives pin to activeLevel for ms: the SDK's 5 ms timer switches it back (at
  // most 5 ms late) even while room.loop() waits on the network. done (optional) then runs from
  // room.loop(). Pulsing a pin that is already pulsing restarts its time.
  // Configure the pin as an OUTPUT in setup() and do not write it yourself
  // while it pulses. Room reset ends every pulse without calling done.
  bool pulse(uint8_t pin, uint8_t activeLevel, uint32_t ms, room::Action done = nullptr);
  // The next ed::Input press in the order presses happened (nullptr if none),
  // for sequences that span several inputs. Up to 16 presses wait while the
  // loop is busy; further presses are dropped and counted by lostPresses().
  Input *nextPress();
  // Presses dropped because 16 were already waiting, since the last call.
  // Restart a sequence when it is non-zero: its order is no longer complete.
  uint8_t lostPresses();
  // Validates the declarations and starts networking. Returns false, and keeps
  // the sketch local-only, when a declaration breaks the contract (the reason
  // is printed on USB serial).
#ifdef ED_BUNDLED_BOARD
  bool begin() { return begin(detail::defaultBoard()); }
#endif
  // For a board without a bundled adapter (see BOARD_PORTING.md). Keep board
  // alive for the whole sketch.
  bool begin(BoardAdapter &board);
  void loop();

  // CustomDriver, called by the SDK core from room.loop().
  void state(JsonObject report) override;
  bool command(const char *propId, const char *capability, bool testing) override;
  void resetRoom() override;
  void endTest() override;
  void testLease(uint32_t deadlineMs) override { testDeadline = deadlineMs; }
  // DescriptionSource
  size_t descriptionLength() override;
  void writeDescription(Print &out) override;
  const char *firmwareVersion() override { return version; }
  bool declares(const char *propId, const char *capability, bool command, bool testing) override;

protected:
  RoomBase(const char *name, const char *version, room::Prop *props, Prop *handles,
           size_t propCapacity, room::Entry *entries, size_t entryCapacity,
           room::Detail *details, size_t detailCapacity)
      : registry(props, propCapacity, entries, entryCapacity, details, detailCapacity),
        handles(handles), name(name), version(version) {}

private:
  friend class Prop;
  friend class Command;
  static void tick();
  void stopTests();
  // An output held by the SDK: switched back by the timer interrupt at offAt.
  // pin stays assigned after the output ends so a restart reuses the slot.
  struct TimedPin {
    volatile int16_t pin = -1;
    volatile bool on = false, finished = false;
    volatile uint8_t idleLevel = 0;
    volatile uint32_t offAt = 0;
    room::Action done = nullptr;
  };
  static TimedPin *slotFor(TimedPin *pins, size_t count, uint8_t pin);
  static void start(TimedPin &slot, uint8_t pin, uint8_t activeLevel, uint32_t ms,
                    room::Action done);
  static void expire(TimedPin *pins, size_t count, uint32_t now, uint32_t lease);
  static void end(TimedPin *pins, size_t count);
  TimedPin testPins[4];
  TimedPin pulses[8];
  // Presses in order, written by the timer and consumed from the loop with the
  // timer masked.
  struct QueuedPress {
    Input *input;
    uint32_t atMs, game;
    bool seen; // room.loop() has returned since it was queued
  };
  static constexpr uint8_t PRESS_CAPACITY = 16;
  QueuedPress presses[PRESS_CAPACITY] = {};
  volatile uint8_t pressHead = 0, pressCount = 0, lost = 0;
  bool drained = false; // nextPress() was called during this loop() pass
  bool timerRunning = false;
  friend class Input;
  void queuePress(Input *input, uint32_t now, uint32_t game);
  bool takePress(Input *input);
  QueuedPress &pressAt(uint8_t i) { return presses[(pressHead + i) % PRESS_CAPACITY]; }
  void dropIgnoredPresses();
  void markPressesSeen();
  room::Registry registry;
  Prop *handles;
  Prop invalid;
  const char *name, *version;
  room::Scheduler<8> scheduler;
  room::Action resetHandler = nullptr;
  volatile uint32_t testDeadline = 0;
  bool inTestStart = false, networking = false;
};

// Capacities are compile-time: props, signals+commands+state entries, and
// details (Test behaviors plus number/enum fields).
template <size_t Props = 8, size_t Entries = 48, size_t Details = 16>
class BasicRoom : public RoomBase {
public:
  BasicRoom(const char *name, const char *firmwareVersion)
      : RoomBase(name, firmwareVersion, props, handles, Props, entries, Entries, details, Details) {}

private:
  room::Prop props[Props];
  Prop handles[Props];
  room::Entry entries[Entries];
  room::Detail details[Details];
};
using Room = BasicRoom<>;

} // namespace ed
