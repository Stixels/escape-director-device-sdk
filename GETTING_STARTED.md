# Connect your custom sketch

Use this guide if you write your own Arduino sketches. It installs the SDK,
uploads a two-prop example and shows how to adapt it. Pairing, saving props and
updating firmware in the app are covered in
[Build Your Own Controller](https://docs.escapedirector.com/build-your-rooms/build-your-own-controller).

This candidate includes adapters for **Arduino UNO R4 WiFi** and **Arduino GIGA R1 WiFi**; see [qualification status](docs/compatibility.md). For
another board, see [Add a board adapter](BOARD_PORTING.md). Start with a spare
board: uploading replaces its sketch.

## Bring your sketch and a coding agent

You can keep your own puzzle code and use an AI coding agent to add the SDK.
Extract this download, place a copy of your sketch in that folder, and open the
folder in your agent. `AGENTS.md` explains the API, integration steps, identity
rules and validation; `CLAUDE.md` points to the same guide.

Start with this prompt, replacing the bracketed details:

> Read AGENTS.md and integrate the Escape Director SDK into [sketch folder] for
> [exact board]. Preserve my existing puzzle logic, pins, output polarity and
> reset behavior. I want [what solves each prop] and [commands and state to show
> in Escape Director]. Inspect the code first and ask about anything the wiring
> or behavior does not establish. Use the bundled API and example, keep existing
> prop IDs, validate the description and compile for my board. Tell me what
> changed, what you verified, and what still needs a physical test.

Use the example below if you are starting from scratch. An agent can help adapt
code for a supported adapter; this does not make arbitrary boards or libraries
compatible automatically.

## 1. Install the libraries

1. Install [Arduino IDE 2](https://www.arduino.cc/en/software).
2. Open **Boards Manager** and install **Arduino UNO R4 Boards** or
   **Arduino Mbed OS Giga Boards**.
3. Open **Library Manager** and install **ArduinoJson** (version 7) and
   **ArduinoMqttClient**.
4. For a packaged download, extract the SDK bundle. In Arduino IDE, choose
   **Sketch → Include Library → Add .ZIP Library…** and select the
   `EscapeDirector` ZIP inside it, not the SDK download itself.
   If you cloned this source repository, use the CLI path below or build a
   download with the instructions in `CONTRIBUTING.md`.

### Arduino CLI from this source folder

```sh
arduino-cli core update-index
arduino-cli core install arduino:renesas_uno@1.5.3
arduino-cli core install arduino:mbed_giga@4.6.0
arduino-cli lib install ArduinoJson@7.4.3 ArduinoMqttClient@0.1.8
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi --library . examples/two_props
arduino-cli compile --fqbn arduino:mbed_giga:giga --library . examples/two_props
```

Install only the core you need if you use one board. These commands compile;
USB upload is a separate step. On Windows, run the same commands in PowerShell.
No global `enable_unsafe_install` setting is needed for this source path.

See [Arduino's library installation guide](https://support.arduino.cc/hc/en-us/articles/5145457742236-Install-libraries-in-the-Arduino-IDE)
if you need help finding these menus.

## 2. Upload the example

1. On a GIGA, attach its external Wi-Fi antenna.
2. Choose **File → Examples → EscapeDirector → two_props**.
3. Select your board and its USB port. On a GIGA, use the main M7 processor.
4. Click **Verify**, then **Upload**. Keep the board connected.

The example has two props, each with an LED:

| Prop        | Input | GIGA output       | UNO output                           |
| ----------- | ----- | ----------------- | ------------------------------------ |
| Three taps  | D2    | Built-in blue LED | Built-in LED                         |
| Hold button | D3    | Built-in red LED  | D12 (add an LED and resistor to GND) |

You can try its commands without wiring; the app also shows each LED's state.
To check the inputs, disconnect power and connect normally-open buttons from D2
and D3 to GND. The sketch enables pull-ups; do not connect these inputs to a
voltage supply.

## 3. Pair it and save its props

On the same Room Station computer, start Room Connector 0.8.5 and open the
Room's **Devices** page in Escape Director. Add an Integrated Device using
**Your own firmware (SDK)**, connect USB and use **Pair controller** to approve
the sketch and set its Wi-Fi connection. Wait for station verification, then
choose **Save props to Room**. You should see **Three taps** and **Hold button**.

For a changed description on the same paired controller, use **Controller setup
→ Update firmware connection**, then **Save props to Room** again. The
[Product Guide](https://docs.escapedirector.com/build-your-rooms/build-your-own-controller)
provides the full app walkthrough when published; this candidate's guide and
product links must be qualified before public release.

## 4. Check the example

In **Test mode**:

1. Run **Pulse LED** for each prop and watch its LED. Each pulse stops on its own.
2. If you wired buttons, press and release D2 three times: Three taps' LED stays
   on until **Reset**. Hold D3: Hold button's LED is on only while pressed.
3. Release both inputs and turn off Test mode. Physical solves remain normal
   puzzle state; leaving Test mode only removes temporary pulse effects.

**Complete prop** and **Reset** change ordinary puzzle state, so this example
does not expose them as Test commands. Exercise them in the practice game or
normal idle control flow where available. Complete prop keeps the output on
until Reset; Reset Room resets both props.

Then link each prop to a Puzzle and run a practice game. Complete the Puzzle in
the Dashboard and check that the prop completes; reset, then solve the prop and
check that the Puzzle completes once. If the timer cannot start, the example
prints a diagnostic and runs local input/output logic only. The prop then stays
offline in Escape Director: it cannot pair, report state or receive commands
until the board restarts with a working timer.

## Make it your own

Save a copy of the example before editing it.

| File            | Contains                                                     |
| --------------- | ------------------------------------------------------------ |
| `TwoProps.h`    | Puzzle logic, independent of the board.                      |
| `two_props.ino` | Pins, the board timer and the SDK calls.                     |
| `Description.h` | The props the app shows: names, signals, commands and state. |

`description.json` is a copy of the JSON in `Description.h` for reading and
validation. Keep the two identical. `description.schema.json` in this folder
lets editors and validators check it. The whole description must be 16 KiB or
less; it stays in flash, so its size doesn't use the board's RAM.

**Describing props.** Each prop has a UUID and lists:

- **signals** — something that happens, such as Completed;
- **commands** — an action your sketch handles, such as Reset. Mark it
  `testable` to allow it in Test mode;
- **state** — a current value: boolean, number or enum.

Keep a prop's UUID and its capability IDs when you rename them, so saved links
and Automations keep working. Give a new prop its own UUID. To let a prop be
linked to a Puzzle, add `"completion": { "signal": "completed", "command": "complete" }`
naming one of its signals and commands. You can also add readable descriptions,
units and enum labels. See [the protocol reference](protocol.md#description-reference)
for every field and limit.

**Writing the logic.**

- Keep the main loop responsive: use timed states instead of long `delay()` calls
  or loops that wait for a player, and keep calling `ed::poll()`.
- Read inputs and end timed outputs in the function passed to `ed::startTimer`.
  It runs on a board timer even while networking blocks the loop.
- Route physical inputs and commands through the same puzzle logic. Writing only
  a relay pin from a command can be undone by the next loop.
- Keep temporary Test effects separate from puzzle state, and keep the example's
  timer-enforced Test expiry.

After uploading a changed description, follow **Update your firmware** in
[Build Your Own Controller](https://docs.escapedirector.com/build-your-rooms/build-your-own-controller#5-update-your-firmware).

## If the controller does not appear

Open the Device's **Controller setup**. If it reports that the description is
invalid, expand **Developer details** for the fields to fix. See
[Troubleshooting](https://docs.escapedirector.com/build-your-rooms/build-your-own-controller#troubleshooting)
for other messages.
