// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#include "EscapeDirectorRoom.h"
#include <Arduino.h>

namespace ed {
namespace {
RoomBase *activeRoom = nullptr;

struct PrintSink {
  Print &out;
  void write(char c) { out.write(uint8_t(c)); }
};

bool expired(uint32_t now, uint32_t deadline) {
  return int32_t(now - deadline) >= 0;
}
// Cores differ on digitalWrite's level type (int or PinStatus).
void writePin(int16_t pin, uint8_t level) { digitalWrite(uint8_t(pin), level ? HIGH : LOW); }
} // namespace

// ---- Input -----------------------------------------------------------------

Input *Input::first = nullptr;

Input::Input(uint8_t pin, uint8_t mode, bool activeLow, uint16_t debounceMs)
    : pin(pin), mode(mode), activeLow(activeLow),
      debounceSamples(uint8_t(debounceMs / 5 ? (debounceMs / 5 > 255 ? 255 : debounceMs / 5) : 1)) {
  next = first;
  first = this;
}

// Timer context (or room.loop() when no timer is available).
void Input::sample(uint32_t now, uint32_t game) {
  const bool raw = (digitalRead(pin) == LOW) == activeLow;
  if (raw == stable) {
    changing = 0;
    return;
  }
  if (++changing < debounceSamples)
    return;
  changing = 0;
  stable = raw;
  if (raw && activeRoom)
    activeRoom->queuePress(this, now, game);
}

bool Input::pressed() {
  asked = true;
  return activeRoom && activeRoom->takePress(this);
}

// ---- Command ---------------------------------------------------------------

// A declaration that failed (for example on an invalid prop) returns a Command
// without a Room or entry; begin() reports the recorded problem.
Command &Command::testPin(uint8_t pin, uint8_t activeLevel, uint32_t durationMs) {
  if (!room || entry < 0)
    return *this;
  if (room::Detail *d = room->registry.detailFor(entry)) {
    room->registry.entries[entry].flags |= room::Testable;
    d->testPin = int16_t(pin);
    d->testLevel = activeLevel ? 1 : 0;
    d->testMs = durationMs ? durationMs : 1;
  }
  return *this;
}

Command &Command::test(room::Action start, room::Action stop) {
  if (!room || entry < 0)
    return *this;
  if (!start || !stop) {
    if (!room->registry.problem)
      room->registry.problem = "test() needs both a start and a stop action";
    return *this;
  }
  if (room::Detail *d = room->registry.detailFor(entry)) {
    room->registry.entries[entry].flags |= room::Testable;
    d->testStart = start;
    d->testStop = stop;
  }
  return *this;
}

// ---- Prop ------------------------------------------------------------------

Prop &Prop::signal(const char *id, const char *name) {
  if (room)
    room->registry.add(index, room::Kind::Signal, id, name);
  return *this;
}

Command Prop::command(const char *id, const char *name, room::Action handler) {
  if (!room)
    return Command(nullptr, -1);
  const int entry = room->registry.add(index, room::Kind::Command, id, name);
  if (entry >= 0)
    room->registry.entries[entry].ref.action = handler;
  return Command(room, entry);
}

Prop &Prop::state(const char *id, const char *name, const bool *value) {
  if (!room)
    return *this;
  const int entry = room->registry.add(index, room::Kind::Boolean, id, name);
  if (entry >= 0)
    room->registry.entries[entry].ref.boolean = value;
  return *this;
}

Prop &Prop::state(const char *id, const char *name, const int *value, int min, int max,
                  const char *unit) {
  static_assert(sizeof(int) == sizeof(int32_t), "int state requires a 32-bit int");
  return number(id, name, reinterpret_cast<const int32_t *>(value), min, max, unit);
}

#if LONG_MAX == INT32_MAX
Prop &Prop::state(const char *id, const char *name, const long *value, long min, long max,
                  const char *unit) {
  static_assert(sizeof(long) == sizeof(int32_t), "long state requires a 32-bit long");
  return number(id, name, reinterpret_cast<const int32_t *>(value), int32_t(min),
                int32_t(max), unit);
}
#endif

Prop &Prop::number(const char *id, const char *name, const int32_t *value, int32_t min,
                   int32_t max, const char *unit) {
  if (!room)
    return *this;
  const int entry = room->registry.add(index, room::Kind::Number, id, name);
  if (entry < 0)
    return *this;
  room->registry.entries[entry].ref.number = value;
  if (room::Detail *d = room->registry.detailFor(entry)) {
    d->min = min;
    d->max = max;
    d->unit = unit;
  }
  return *this;
}

Prop &Prop::enumState(const char *id, const char *name, const uint8_t *index_,
                      const char *const *values, size_t count) {
  if (!room)
    return *this;
  const int entry = room->registry.add(index, room::Kind::Enum, id, name);
  if (entry < 0)
    return *this;
  room->registry.entries[entry].ref.index = index_;
  if (room::Detail *d = room->registry.detailFor(entry)) {
    d->values = values;
    d->valueCount = uint8_t(count > 255 ? 255 : count);
  }
  return *this;
}

Prop &Prop::completion(const char *signal, const char *command) {
  if (room && index < room->registry.propCount) {
    room->registry.props[index].completionSignal = signal;
    room->registry.props[index].completionCommand = command;
  }
  return *this;
}

bool Prop::trigger(const char *signal) { return report(signal, ed::captureGame(), millis()); }

bool Prop::trigger(const char *signal, const Input &cause) {
  return report(signal, cause.pressGame, cause.pressAt);
}

bool Prop::report(const char *signal, uint32_t game, uint32_t atMs) {
  if (!room || !room->networking || index >= room->registry.propCount ||
      room->registry.find(index, room::Kind::Signal, signal) < 0)
    return false;
  char id[37];
  room->registry.propId(index, id);
  return ed::signal(id, signal, game, atMs);
}

bool Prop::triggerWhen(const char *signal, bool condition) {
  if (!room || index >= room->registry.propCount)
    return false;
  const int entry = room->registry.find(index, room::Kind::Signal, signal);
  if (entry < 0)
    return false;
  uint8_t &flags = room->registry.entries[entry].flags;
  const bool rising = condition && !(flags & room::LastCondition);
  flags = condition ? uint8_t(flags | room::LastCondition) : uint8_t(flags & ~room::LastCondition);
  if (rising)
    trigger(signal);
  return rising;
}

// ---- Room ------------------------------------------------------------------

Prop &RoomBase::prop(const char *slug, const char *name, const char *uuid) {
  const int index = registry.addProp(slug, name, uuid);
  if (index < 0)
    return invalid;
  handles[index].room = this;
  handles[index].index = uint8_t(index);
  return handles[index];
}

bool RoomBase::after(uint32_t ms, room::Action action) {
  return scheduler.after(action, ms, millis(), inTestStart);
}

bool RoomBase::begin(BoardAdapter &board) {
  activeRoom = this;
  for (Input *input = Input::first; input; input = input->next)
    pinMode(input->pin, static_cast<decltype(INPUT_PULLUP)>(input->mode));
  // Opens USB serial, so a rejected declaration can be reported.
  board.begin();
  Stream &usb = board.setupStream();
  // Inputs, pulses and Test pins run from the timer even while networking
  // blocks, and even when the sketch stays local-only.
  timerRunning = board.startTimer(tick, 5);
  if (const char *problem = registry.validate(name, version)) {
    usb.print(F("Escape Director: "));
    usb.print(problem);
    usb.println(F(". The puzzle runs locally; fix the declaration to connect."));
    return false;
  }
  if (!timerRunning) {
    usb.println(F("Escape Director: SDK timer unavailable; the puzzle runs locally."));
    return false;
  }
  ed::begin(static_cast<DescriptionSource &>(*this), static_cast<CustomDriver &>(*this), board);
  networking = true;
  return true;
}

RoomBase::TimedPin *RoomBase::slotFor(TimedPin *pins, size_t count, uint8_t pin) {
  TimedPin *free = nullptr;
  for (size_t i = 0; i < count; ++i) {
    if (pins[i].pin == pin)
      return &pins[i];
    if (!free && !pins[i].on && !pins[i].finished)
      free = &pins[i];
  }
  return free;
}

// Loop context. Restarting a pin's slot also cancels a done action that has
// not run yet, so it cannot report an older pulse as ended.
void RoomBase::start(TimedPin &slot, uint8_t pin, uint8_t activeLevel, uint32_t ms,
                     room::Action done) {
  slot.on = false; // hold the slot away from the timer while it is rewritten
  slot.finished = false;
  slot.pin = int16_t(pin);
  slot.idleLevel = activeLevel ? 0 : 1;
  slot.offAt = millis() + (ms ? ms : 1);
  slot.done = done;
  writePin(pin, activeLevel);
  slot.on = true;
}

// Timer context: switches outputs back at their deadline, or at lease expiry.
void RoomBase::expire(TimedPin *pins, size_t count, uint32_t now, uint32_t lease) {
  for (size_t i = 0; i < count; ++i) {
    TimedPin &p = pins[i];
    if (p.on && (expired(now, p.offAt) || (lease && expired(now, lease)))) {
      writePin(p.pin, p.idleLevel);
      p.on = false;
      p.finished = true;
    }
  }
}

// Loop context: switches every output back now, without done actions.
void RoomBase::end(TimedPin *pins, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    TimedPin &p = pins[i];
    const bool wasOn = p.on;
    p.on = false;
    p.finished = false;
    if (wasOn)
      writePin(p.pin, p.idleLevel);
  }
}

bool RoomBase::pulse(uint8_t pin, uint8_t activeLevel, uint32_t ms, room::Action done) {
  TimedPin *slot = slotFor(pulses, 8, pin);
  if (!slot)
    return false;
  start(*slot, pin, activeLevel, ms, done);
  return true;
}

// Timer context. When 16 presses already wait, the new one is dropped and
// counted: the waiting prefix stays exactly in order.
void RoomBase::queuePress(Input *input, uint32_t now, uint32_t game) {
  if (pressCount >= PRESS_CAPACITY) {
    if (lost < 255)
      lost = uint8_t(lost + 1);
    return;
  }
  pressAt(pressCount) = QueuedPress{input, now, game, false};
  pressCount = uint8_t(pressCount + 1);
}

bool RoomBase::takePress(Input *input) {
  noInterrupts();
  bool found = false;
  for (uint8_t i = 0; i < pressCount && !found; ++i) {
    if (pressAt(i).input != input)
      continue;
    found = true;
    input->pressAt = pressAt(i).atMs;
    input->pressGame = pressAt(i).game;
    // Close the gap so later presses keep their order.
    for (uint8_t j = i; j + 1 < pressCount; ++j)
      pressAt(j) = pressAt(uint8_t(j + 1));
    pressCount = uint8_t(pressCount - 1);
  }
  interrupts();
  return found;
}

Input *RoomBase::nextPress() {
  drained = true;
  noInterrupts();
  Input *input = nullptr;
  if (pressCount) {
    const QueuedPress &press = pressAt(0);
    input = press.input;
    input->pressAt = press.atMs;
    input->pressGame = press.game;
    pressHead = uint8_t((pressHead + 1) % PRESS_CAPACITY);
    pressCount = uint8_t(pressCount - 1);
  }
  interrupts();
  return input;
}

uint8_t RoomBase::lostPresses() {
  noInterrupts();
  const uint8_t count = lost;
  lost = 0;
  interrupts();
  return count;
}

// Start of a loop() pass: drop presses the previous pass could read but did not
// ask for, unless it read presses in order with nextPress().
void RoomBase::dropIgnoredPresses() {
  noInterrupts();
  if (!drained) {
    uint8_t kept = 0;
    for (uint8_t i = 0; i < pressCount; ++i) {
      const QueuedPress press = pressAt(i);
      if (!press.seen || press.input->asked)
        pressAt(kept++) = press;
    }
    pressCount = kept;
  }
  interrupts();
  drained = false;
  for (Input *input = Input::first; input; input = input->next)
    input->asked = false;
}

// End of room.loop(): the sketch can now read every waiting press.
void RoomBase::markPressesSeen() {
  noInterrupts();
  for (uint8_t i = 0; i < pressCount; ++i)
    pressAt(i).seen = true;
  interrupts();
}

void RoomBase::loop() {
  dropIgnoredPresses();
#ifdef ED_BENCH_TRACE
  { // Bench only: how long each network service pass holds the sketch.
    static uint32_t calls = 0, over5 = 0, over20 = 0, over50 = 0, longest = 0, since = millis();
    const uint32_t start = millis();
    if (networking)
      ed::poll();
    const uint32_t took = millis() - start;
    ++calls;
    over5 += took > 5;
    over20 += took > 20;
    over50 += took > 50;
    if (took > longest)
      longest = took;
    if (millis() - since >= 10000) {
      Serial.print("ED loop calls=");
      Serial.print(calls);
      Serial.print(" over5ms=");
      Serial.print(over5);
      Serial.print(" over20ms=");
      Serial.print(over20);
      Serial.print(" over50ms=");
      Serial.print(over50);
      Serial.print(" longestMs=");
      Serial.println(longest);
      calls = over5 = over20 = over50 = longest = 0;
      since = millis();
    }
  }
#else
  if (networking)
    ed::poll();
#endif
  if (!timerRunning)
    tick();
  for (TimedPin &p : pulses)
    if (p.finished) {
      p.finished = false;
      if (p.done)
        p.done();
    }
  for (TimedPin &p : testPins)
    p.finished = false;
  const uint32_t now = millis();
  // Callback Test effects end from here; Test pins are switched off by tick().
  if (testDeadline && expired(now, testDeadline)) {
    testDeadline = 0;
    stopTests();
  }
  scheduler.runDue(now);
  markPressesSeen();
}

// Interrupt context: samples inputs and switches pulses and Test pins off at
// their deadlines. No sketch code runs here.
void RoomBase::tick() {
  RoomBase *self = activeRoom;
  if (!self)
    return;
  const uint32_t now = millis();
  const uint32_t game = ed::captureGame();
  for (Input *input = Input::first; input; input = input->next)
    input->sample(now, game);
  expire(self->pulses, 8, now, 0);
  expire(self->testPins, 4, now, self->testDeadline);
}

void RoomBase::stopTests() {
  end(testPins, 4);
  for (size_t i = 0; i < registry.entryCount; ++i) {
    room::Entry &e = registry.entries[i];
    if (!(e.flags & room::TestActive))
      continue;
    e.flags &= uint8_t(~room::TestActive);
    if (e.detail >= 0 && registry.details[e.detail].testStop)
      registry.details[e.detail].testStop();
  }
  scheduler.cancelTestScoped();
}

void RoomBase::state(JsonObject report) {
  JsonArray values = report["props"].to<JsonArray>();
  char id[37];
  for (size_t p = 0; p < registry.propCount; ++p) {
    JsonObject prop = values.add<JsonObject>();
    registry.propId(uint8_t(p), id);
    prop["id"] = id;
    JsonObject fields = prop["values"].to<JsonObject>();
    for (size_t i = 0; i < registry.entryCount; ++i) {
      const room::Entry &e = registry.entries[i];
      if (e.prop != p)
        continue;
      const room::Detail *d = e.detail >= 0 ? &registry.details[e.detail] : nullptr;
      if (e.kind == room::Kind::Boolean) {
        fields[e.id] = e.ref.boolean && *e.ref.boolean;
      } else if (e.kind == room::Kind::Number) {
        int32_t value = e.ref.number ? *e.ref.number : 0;
        if (d) // Reports must stay within the declared range.
          value = value < d->min ? d->min : value > d->max ? d->max : value;
        fields[e.id] = value;
      } else if (e.kind == room::Kind::Enum && d && d->valueCount) {
        const uint8_t selected = e.ref.index ? *e.ref.index : 0;
        fields[e.id] = d->values[selected < d->valueCount ? selected : 0];
      }
    }
  }
}

bool RoomBase::command(const char *propId, const char *capability, bool testing) {
  const int prop = registry.findProp(propId);
  const int entry = prop < 0 ? -1 : registry.find(uint8_t(prop), room::Kind::Command, capability);
  if (entry < 0)
    return false;
  room::Entry &e = registry.entries[entry];
  if (!testing) {
    if (!e.ref.action)
      return false;
    e.ref.action();
    return true;
  }
  if (!(e.flags & room::Testable) || e.detail < 0)
    return false;
  const room::Detail &d = registry.details[e.detail];
  if (d.testPin >= 0) {
    TimedPin *slot = slotFor(testPins, 4, uint8_t(d.testPin));
    if (!slot)
      return false;
    start(*slot, uint8_t(d.testPin), d.testLevel, d.testMs, nullptr);
    return true;
  }
  if (!d.testStart)
    return false;
  inTestStart = true;
  d.testStart();
  inTestStart = false;
  e.flags |= room::TestActive;
  return true;
}

void RoomBase::resetRoom() {
  scheduler.cancelAll();
  stopTests();
  end(pulses, 8);
  noInterrupts();
  pressCount = 0; // presses from before the reset are stale
  lost = 0;
  interrupts();
  if (resetHandler)
    resetHandler();
}

void RoomBase::endTest() {
  testDeadline = 0;
  stopTests();
}

size_t RoomBase::descriptionLength() { return registry.descriptionLength(name, version); }

void RoomBase::writeDescription(Print &out) {
  PrintSink sink{out};
  registry.writeDescription(sink, name, version);
}

bool RoomBase::declares(const char *propId, const char *capability, bool command, bool testing) {
  const int prop = registry.findProp(propId);
  if (prop < 0)
    return false;
  const int entry =
      registry.find(uint8_t(prop), command ? room::Kind::Command : room::Kind::Signal, capability);
  return entry >= 0 && (!testing || (registry.entries[entry].flags & room::Testable));
}

} // namespace ed
