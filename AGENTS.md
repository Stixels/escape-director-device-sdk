# Escape Director SDK: guide for coding agents

This SDK connects a person's own Arduino sketch to Escape Director. Their
sketch runs the prop; Escape Director shows its state, sends its commands and
uses its signals in Automations and linked Puzzles. Read this before editing a
sketch for the SDK.

## Start with the existing sketch

The SDK is an integration layer, not a replacement for the person's puzzle
logic. Keep working code, pin assignments, output polarity, manual overrides and
reset behavior unless the integration requires a specific change. Work on a
copy of a deployed sketch. Do not rewrite it into the example's puzzle.

1. Read this guide, `docs/room-api.md`, `src/EscapeDirectorRoom.h` and
   `examples/room_basic`. Keep the person's sketch under the folder containing
   `AGENTS.md` so these instructions apply, or load this guide explicitly.
   `examples/button_code` shows a sequence across several inputs.
2. Inspect the sketch before asking questions. Establish the exact board,
   inputs/outputs and polarity, what solves each prop, how it resets, and which
   actions the Game Master should have. Ask only for facts that the sketch and
   wiring notes do not establish. Never guess electrical behavior.
3. Map each puzzle to a prop: its solved signal, Game Master commands, live
   state, and Test behaviors for effects that are safe to try. For a Device
   that is already paired, keep its prop UUIDs and every capability ID (see
   **IDs** below). Explain any removed capability: saved Room references may
   need repair.
4. Integrate with `ed::Room` (below). Leave Wi-Fi, pairing, credentials, TLS,
   MQTT and reconnection to the library.
5. Compile for the exact board and report the result. Keep software checks
   separate from upload, pairing and physical observations.

## `ed::Room`

`ed::Room` is the SDK's only sketch API. Follow `docs/room-api.md`:

- Declare props, signals, commands, state and Test behaviors in `setup()`, then
  call `room.begin()`; call `room.loop()` at the top of every `loop()`. The
  description is generated from the declarations; there is no JSON file.
- Buttons and switches become `ed::Input`: `pressed()` once per press,
  `active()` for the debounced level, `room.nextPress()` for sequences across
  several inputs. Up to 16 presses wait while the network pauses the loop;
  restart a sequence when `room.lostPresses()` is non-zero.
- A signal caused by a press uses `prop.trigger(signal, input)`; a transition
  detected from current state uses `prop.trigger(signal)` or
  `prop.triggerWhen(signal, condition)`. Never infer a signal repeatedly from
  unchanged state.
- Replace `delay()`: outputs that must end on time use `room.pulse()`, waits
  between steps use `room.after()`. No blocking loops that wait for a player.
- Commands run the same functions local inputs use. Do not only write a relay
  pin from a command if the next `loop()` will restore it from unchanged state.
- Test behaviors (`.testPin()` or `.test(start, stop)`) must be temporary and
  must not change ordinary puzzle state.
- No user code runs in an interrupt. Do not add timers or `noInterrupts()` for
  SDK reasons.

`examples/button_code` shows a sequence across several inputs, a delayed
action and live progress.

## IDs

A prop's UUID derives from its slug (`room.prop("exit-door", ...)`); signal,
command and state IDs are the strings you declare. They are permanent: never
change or reuse them; rename with `name`. When moving a Device that is already
paired, pass its existing prop UUID: `room.prop("fuses", "Fuse panel", "<uuid>")`.

## Rules

1. **Serial is the setup channel.** Remove `Serial.begin()` at other speeds.
   Printing is fine; never print lines that start with `{`.
2. **Mind 32 KB of RAM on the UNO R4.** Avoid `String` building in the loop,
   large buffers and large `JsonDocument`s.
3. **Limits.** Per Room: 8 props by default (`ed::BasicRoom<Props, Entries,
   Details>` raises capacities). Per prop: 8 signals, 8 commands, 16 state
   fields. IDs: lowercase letters, digits and hyphens, starting with a letter,
   at most 40 characters. `room.begin()` rejects a declaration that breaks a
   rule and prints why on USB serial.

## Files

| File | Purpose |
| --- | --- |
| `EscapeDirector-<version>.zip` | Arduino-installable library in the packaged download; source is also in `src/`. |
| `src/EscapeDirectorRoom.h` | `ed::Room`, `ed::Prop`, `ed::Input`: the sketch API. |
| `examples/room_basic/room_basic.ino` | A complete one-prop sketch. |
| `examples/button_code/button_code.ino` | A sequence puzzle: several inputs, `room.nextPress()`, `room.after()`. |
| `docs/room-api.md` | The guide: every call, what pauses during an outage, porting checklist. |
| `BOARD_PORTING.md` | Adding a board without a bundled adapter. |
| `protocol.md`, `description.schema.json` | The wire protocol and generated description, for SDK maintainers. |

Bundled adapters: Arduino GIGA R1 WiFi (`arduino:mbed_giga:giga`) and Arduino
UNO R4 WiFi (`arduino:renesas_uno:unor4wifi`). Other boards need an adapter.

## Verify your change

Install the board core and libraries using `GETTING_STARTED.md`, then compile
for the actual target board:

```sh
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi --library . workspace/my_prop
```

To change the SDK itself, run `npm ci` and `npm run check` in this repository.

## Hand off to a person

Report the changed files, preserved IDs and wiring, the exact compile command
and result, library/core versions, and any checks you could not run. Do not
label a compile pass as a working physical integration. If the user has
authorized an upload or you have bench tools, perform only the checks the
available hardware can establish and record the actual observations. Otherwise
hand off these steps:

1. Upload it with prop loads disconnected.
2. In Escape Director, pair a new Device (**Your own firmware (SDK)**), or for
   an existing Device choose **Controller setup → Check controller → Update
   connection**, then **Save props to Room**.
3. In **Test mode**, run each testable command and watch the physical output.
4. Run a practice game to check signals, linked Puzzles and Automations.
