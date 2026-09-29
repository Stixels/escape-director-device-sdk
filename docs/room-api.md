# Integrating a sketch with `ed::Room`

`ed::Room` connects a sketch to Escape Director. You
declare what the Room should see beside the code that uses it; the SDK
generates the description, derives stable IDs, and handles Wi-Fi, pairing,
reconnection, Game and Test rules. Your puzzle logic stays ordinary Arduino
code in `loop()`.

## A complete example

```cpp
#include <EscapeDirectorRoom.h>

constexpr uint8_t DOOR_RELAY = LED_BUILTIN; // your relay pin in a real prop

ed::Room room("Train door", "1.0.0");
ed::Prop &door = room.prop("door", "Train door");
ed::Input lever(2); // sampled by the SDK, so a pull during a reconnect is kept

bool doorOpen = false;

void doorClosed() { doorOpen = false; }
void openDoor() {
  // Five seconds (to within the SDK's 5 ms timer), even while the board is
  // reconnecting.
  room.pulse(DOOR_RELAY, HIGH, 5000, doorClosed);
  doorOpen = true;
}
void closeDoor() {
  digitalWrite(DOOR_RELAY, LOW);
  doorOpen = false;
}

void setup() {
  pinMode(DOOR_RELAY, OUTPUT);
  closeDoor();

  door.signal("solved", "Lever pulled");
  door.command("open", "Open door", openDoor);
  door.command("reset", "Close door", closeDoor);
  door.command("pulse", "Pulse door").testPin(DOOR_RELAY, HIGH, 1000);
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
```

This is [examples/room_basic](../examples/room_basic/room_basic.ino). The built-in
LED stands in for the door relay; touch pin 2 to GND to pull the lever. For a
sequence across several inputs, see [examples/button_code](../examples/button_code/button_code.ino).

## Declarations

Declare everything in `setup()` before `room.begin()`.

| Call | Meaning |
| --- | --- |
| `ed::Room room(name, firmwareVersion)` | One per sketch. Change the version when you change the firmware. |
| `room.prop(slug, name)` | A prop. `slug` is its permanent ID, for example `"exit-door"`. |
| `room.prop(slug, name, uuid)` | Keep an existing Device's prop UUID (see [IDs](#ids)). |
| `prop.signal(id, name)` | Something the prop reports, used by Automations and linked Puzzles. |
| `prop.command(id, name, handler)` | Something Escape Director can ask it to do. |
| `.testPin(pin, level, ms)` / `.test(start, stop)` | Allow the command in Test mode (see [Test mode](#test-mode)). |
| `prop.state(id, name, &flag)` | Live boolean. |
| `prop.state(id, name, &count, min, max, unit)` | Live number (`int` or `long`); reports stay within `min`–`max`. |
| `prop.state(id, name, &index, values)` | Live enum: `index` selects one of `values`. |
| `prop.completion(signal, command)` | Lets a Puzzle link to the prop. |
| `room.onReset(handler)` | Room reset from Escape Director. |
| `room.begin()` | Validates the declarations and connects. On a board without a bundled adapter, `room.begin(board)` ([board porting](../BOARD_PORTING.md)). |

IDs and enum values use lowercase letters, digits and hyphens, start with a
letter and have at most 40 characters. Names have 1–80 characters. A prop has up
to 8 signals, 8 commands and 16 state fields; a Room has up to 8 props. If a
declaration breaks a rule, `room.begin()` prints the reason on USB serial,
returns `false`, and the sketch runs locally until you fix it.

The default Room reserves space for 48 signals, commands and state fields and 16
Test behaviors plus number/enum fields. Use `ed::BasicRoom<8, 64, 24>` for more.

## Inputs and timed outputs

On the UNO R4 WiFi, network work sometimes pauses `loop()`: while connected, at
most about 0.15 s; while connecting, about 1 s; and during a connection attempt
to a Room Connector that does not answer, about 10 s (see
[What pauses](#what-pauses-during-an-outage)). Two helpers keep hardware on time
through those pauses. Neither runs any of your code in an interrupt.

**`ed::Input`** samples a button or switch every 5 ms, debounces it (20 ms by
default) and remembers presses in order, each with the time and Game in which
it happened.

```cpp
ed::Input keypadButton(4);                 // INPUT_PULLUP, pressed = LOW
ed::Input reedSwitch(5, INPUT_PULLUP);     // read its level with active()

if (keypadButton.pressed()) { ... }        // once per press
if (reedSwitch.active()) { ... }           // debounced level
while (ed::Input *press = room.nextPress()) { ... } // presses in order
```

Use `room.nextPress()` for sequences that span several inputs, such as a
joystick code; it returns every input's presses, so don't also call
`pressed()` for those inputs. Declare inputs globally; `room.begin()`
configures their pins.

Presses wait for your code, with two limits:

- **One loop pass.** A press stays available until the end of the first
  `loop()` pass after `room.loop()` returns it. If that pass neither calls the
  input's `pressed()` nor `room.nextPress()`, the press is dropped. Switches
  read only with `active()` therefore never fill the queue, and a press made
  while your code wasn't waiting for it (for example in an earlier puzzle
  stage) doesn't act later.
- **16 presses.** Up to 16 presses wait while `loop()` is paused. Further
  presses are dropped, never reordered, and `room.lostPresses()` returns how
  many since it was last called. A sequence puzzle should start again when it
  is non-zero:

```cpp
if (room.lostPresses()) step = 0;          // the sequence is incomplete
```

**`room.pulse(pin, level, ms, done)`** holds an output for `ms`, then the SDK's
5 ms timer switches it back: at most 5 ms late, even while `loop()` is paused.
`done` runs later from `room.loop()`, for example to update state. Pulsing a
pin again restarts its time (and cancels a `done` that hasn't run yet).
Configure the pin as an `OUTPUT` and do not write it yourself while it pulses.

**`room.after(ms, action)`** runs `action` from `room.loop()` after `ms`.
Scheduling an action that is pending replaces it; `room.cancel(action)` removes
it. Use it for sequencing (for example "wait 26 s, then open the door"), not
for output timing: it runs late by any network pause.

Room reset cancels pending `after()` actions and ends every pulse (without
calling `done`) before your `onReset` handler runs.

## Signals

- `prop.trigger(signal, input)` reports a signal caused by a press: the one
  `input.pressed()` or `room.nextPress()` most recently returned. It carries
  the press's own time and Game.
- `prop.trigger(signal)` reports the signal now. Use it for transitions your
  code detects from current state, for example when the puzzle becomes solved.
  Call it once, at the transition.
- `prop.triggerWhen(signal, condition)` reports when `condition` changes from
  false to true and returns `true` at that change, so your logic can act on the
  same edge.

Outside a live Game nothing is reported (`triggerWhen()` still returns `true`
at the change). Signals are not queued: a signal from an earlier Game, or more
than 2 s old, is dropped. So a press that waited through a reconnect still
drives your puzzle, but its signal is only reported if the press happened less
than 2 s earlier, in the current Game. Always pass the input when a press caused
the signal; `trigger(signal)` would report an old press as new.

## Test mode

Commands are not testable unless they declare a Test behavior:

- `.testPin(pin, level, ms)` drives the pin for `ms` (at most until Test ends);
  the SDK's timer ends it on time.
- `.test(start, stop)` calls `start`; `stop` runs from `room.loop()` when Test
  ends, its lease expires or the Room resets. `after()` actions scheduled inside
  `start` end with Test.

Test effects must be temporary and must not change ordinary puzzle state.

## IDs

A prop's UUID is derived from its slug, so it is stable across uploads. Keep
slugs and capability IDs when you rename things; change `name` instead.
Changing an ID breaks the Room's saved links and Automations.

When moving an already paired Device to `ed::Room`, pass its existing prop
UUIDs: `room.prop("fuses", "Fuse panel", "0210a18f-c31f-45b7-a35a-e20eb82a6c11")`.
Keep its signal, command and state IDs.

## What pauses during an outage

Longest single pause of `loop()`, measured on UNO R4 WiFi with Room Connector
0.8.5 and a seven-prop room controller:

| Situation | Longest pause | Otherwise |
| --- | --- | --- |
| Connected | 0.15 s | ~25 ms network service every 50 ms; ~0.1 s state heartbeat every 2 s |
| Room Connector stops | 0.09 s; 10 s for a blind connection attempt, at most every 2 minutes | ~60 ms discovery query every 5 s |
| Room Connector returns | 1.0 s, while reconnecting | Reconnected about 3 s after Room Connector started |
| Wi-Fi drops | 0.12 s while offline; 1.0 s while reconnecting | Rejoins in the background; reconnected 9 s after the drop |
| Board starts | 1.3 s to connect; 10 s if Room Connector is down | Then as above |

`ed::Input` presses (up to 16) and `room.pulse()` timing (to 5 ms) are
unaffected by these pauses.

Other code in `loop()` (reading an RFID reader, updating a display, level-driven
relays) waits for them. RFID and display libraries are ordinary code: keep
their calls short and let the SDK remember button presses.

## Porting an existing sketch

1. Keep pins, relay polarity and puzzle rules.
2. Replace `delay()`: timed outputs become `room.pulse()`, waits between steps
   become `room.after()`.
3. Make buttons and switches `ed::Input`; handle momentary presses with
   `pressed()` or `room.nextPress()`, levels with `active()`. Restart sequences
   when `room.lostPresses()` is non-zero.
4. Remove `Serial.begin(...)` with another speed: USB serial is the setup
   channel. Printing is fine; never print lines that start with `{`.
5. Declare each puzzle as a prop: its solved signal, Game Master commands,
   live state, and Test behaviors for effects that are safe to try.
6. Call `trigger()` or `triggerWhen()` where the sketch already detects a solve;
   pass the input when a press caused it.
7. Compile for the exact board, pair the Device, **Save props to Room**, then
   check each command in Test mode and each signal in a practice game.

