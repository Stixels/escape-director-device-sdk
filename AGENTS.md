# Escape Director SDK: guide for coding agents

This SDK connects a person's own Arduino sketch to Escape Director. Their
sketch runs the prop; Escape Director shows its state, sends its commands and
uses its signals in Automations and linked Puzzles. Read this before editing a
sketch that includes `EscapeDirector.h`.

## Start with the existing sketch

The SDK is an integration layer, not a replacement for the person's puzzle
logic. Keep working code, pin assignments, output polarity, manual overrides and
reset behavior unless the requested integration requires a specific change.
Work on a copy of a deployed sketch. Do not rewrite it into the example's puzzle.

1. Read this guide, `GETTING_STARTED.md`, the library's `src/EscapeDirector.h`,
   and `examples/two_props`. The source is available directly in `src/` and `examples/` in this repository
   and source bundle. Keep the person's sketch under the folder containing `AGENTS.md`
   so its instructions apply, or explicitly load this guide in the agent.
2. Inspect the sketch before asking questions. Establish the exact board,
   inputs/outputs and polarity, what solves each prop, how it resets, and which
   actions should be available to the Game Master. Ask only for facts that the
   sketch and wiring notes do not establish. Never guess electrical behavior.
3. Map each prop to stable IDs, reported state, completion edges and named
   commands. For an existing paired sketch, retain its IDs. Explain any removed
   capability because saved Room references may need repair.
4. Adapt the existing state machine to the callbacks below. Replace blocking
   waits only where needed to keep local I/O and the SDK responsive. Leave
   pairing, credentials, TLS, MQTT and reconnect logic to the library.
5. Validate the description, compile for the exact board and report the result.
   Keep software checks separate from upload, pairing and physical observations.

## Public API map

Use the bundled header as the authority for signatures; do not invent SDK calls
or implement the wire protocol yourself. There is one `ed::CustomDriver` per
sketch; it may expose several props.

| Hook | Integration responsibility |
| --- | --- |
| `ed::begin(DESCRIPTION, driver)` | Call once in `setup()` after initializing outputs to their intended startup state; keep the description and driver alive. Unbundled boards also pass a `BoardAdapter`. |
| `ed::poll()` | Call on every `loop()` iteration. |
| `ed::startTimer(sample, periodMs)` | Start once after `begin`; check the returned success flag. Sample local inputs and enforce temporary output deadlines in this callback. |
| `state(JsonObject report)` | Snapshot the existing prop state into the declared `props`/`values` shape. |
| `command(propId, capability, testing)` | Dispatch through the same state transitions used by local inputs; return whether handled. Schedule longer actions and return promptly. |
| `resetRoom()` | Explicitly reset all exposed props through their normal reset path. |
| `endTest()` | Remove temporary Test effects without resetting ordinary solved state. |
| `testLease(deadlineMs)` | Store the deadline for the timer callback to enforce even if networking blocks. |
| `ed::captureGame()` and `ed::signal(...)` | Capture authority and time at the physical completion edge; publish from `loop()` using that captured context. |

For example, an existing RFID puzzle keeps its tag reader and solve logic. Its
solved flag becomes reported state; its unsolved-to-solved transition produces
one completion edge; a declared complete/reset command calls the same local
transition functions. Do not directly toggle a relay if the next loop will
restore it from unchanged puzzle state. Signals missed outside a game are not
queued for later. Temporary Test effects must not become ordinary completion.

## Files

| File                                  | Purpose                                                              |
| ------------------------------------- | -------------------------------------------------------------------- |
| `EscapeDirector-<version>.zip`        | Arduino-installable library in the packaged download; source is also in `src/`. |
| `examples/two_props/TwoProps.h`       | Puzzle logic, independent of the board.                              |
| `examples/two_props/two_props.ino`    | Pins, the board timer and the SDK calls.                             |
| `examples/two_props/Description.h`    | The description the app shows: props, signals, commands, state.      |
| `examples/two_props/description.json` | The same JSON as `Description.h`, for validation.                    |
| `description.schema.json`             | JSON Schema (draft 2020-12) for the description.                     |
| `protocol.md`                         | Every description field and limit.                                   |
| `BOARD_PORTING.md`                    | Adding a board without a bundled adapter.                            |

Bundled adapters: Arduino GIGA R1 WiFi (`arduino:mbed_giga:giga`) and Arduino
UNO R4 WiFi (`arduino:renesas_uno:unor4wifi`). Other boards need an adapter.

## Rules

1. **IDs are permanent.** Never change or reuse a prop UUID or a signal,
   command or state ID. Rename with `name`. A new prop gets a new random v4
   UUID in lowercase (`uuidgen | tr A-Z a-z`). Changing an ID breaks the Room's
   saved links and Automations.
2. **One description, two copies.** `Description.h` holds exactly the JSON in
   `description.json`. Keep it a `constexpr` array: it stays in flash and costs
   no RAM. Compact JSON must be at most 16384 bytes.
3. **Report exactly what is declared.** `state()` reports every prop once with
   exactly its declared fields, within each number's `min`/`max` and each
   enum's `values`.
4. **Handle every declared command.** `command()` returns `true` for a declared
   command it handled and `false` otherwise. It returns promptly: schedule
   timed work, never `delay()`.
5. **Signals come from real edges.** Capture `ed::captureGame()` with the time
   when the edge happens, then call `ed::signal(...)` from `loop()`. Never infer
   signals from state.
6. **Keep the loop responsive.** Call `ed::poll()` every loop. No blocking
   `delay()` or loops that wait for a player. Sample inputs and enforce
   deadlines in the callback passed to `ed::startTimer`.
7. **Test mode is temporary.** `testable` commands may only cause effects that
   `endTest()` removes. Stop temporary outputs by the `testLease()` deadline
   inside the timer callback, even if networking blocks the loop.
8. **Share state safely.** Wrap state shared between the timer callback and the
   loop in `noInterrupts()`/`interrupts()`. Keep the timer callback short.
9. **Serial is the setup channel.** Don't print lines that start with `{`.
   Prefer no Serial output at all.
10. **Mind 32 KB of RAM on the UNO R4.** Avoid `String` building in the loop,
    large buffers and large `JsonDocument`s.

To let a prop be linked to a Puzzle, add
`"completion": { "signal": "<signal id>", "command": "<command id>" }`. The
command completes the prop and emits that signal only on a new completion
during a game.

## Verify your change

Install the Arduino board core and libraries using `GETTING_STARTED.md`. For
host checks, run `npm ci` in this repository/source bundle. Then:

```sh
npm run check
npm run validate -- workspace/my_prop/description.json --header workspace/my_prop/Description.h
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi --library . workspace/my_prop
```

Use the actual target board's FQBN. The standalone validator covers both JSON
Schema and semantic checks: byte size, scoped unique IDs, ranges, enum labels,
completion references and header parity. Add `--state state.json` for a sample
state report. `node scripts/validate-description.mjs <file> --json` returns a
machine-readable result and a nonzero exit on failure. See
`docs/agent-integration.md` for the expected header form and handoff checklist.

## Hand off to a person

Report the changed files, preserved IDs and wiring, exact compile command and
result, library/core versions, and any checks you could not run. Do not label a
compile pass as a working physical integration. If the user has authorized an
upload or you have bench tools, perform only the checks the available hardware
can establish and record the actual observations. Otherwise hand off these steps:

1. Upload it with prop loads disconnected.
2. In Escape Director, pair a new Device (**Your own firmware (SDK)**), or for
   an existing Device choose **Controller setup → Update firmware connection**,
   then **Save props to Room**.
3. In **Test mode**, run each testable command and watch the physical output.
4. Run a practice game to check signals, linked Puzzles and Automations.
