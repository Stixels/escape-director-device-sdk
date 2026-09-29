# Connect your sketch

Use this guide if you write your own Arduino sketches. It installs the SDK,
uploads a one-prop example, connects it to a Room and shows how to adapt your
own sketch. The app side is also covered in
[Build Your Own Controller](https://docs.escapedirector.com/build-your-rooms/build-your-own-controller).

The SDK supports the **Arduino UNO R4 WiFi** and **Arduino GIGA R1 WiFi** (see
[compatibility](docs/compatibility.md)); for another board, see
[Add a board adapter](BOARD_PORTING.md). Start with a spare board: uploading
replaces its sketch.

## 1. Install

1. Install [Arduino IDE 2](https://www.arduino.cc/en/software).
2. In **Boards Manager**, install **Arduino UNO R4 Boards** or
   **Arduino Mbed OS Giga Boards**.
3. In **Library Manager**, install **ArduinoJson** (version 7) and
   **ArduinoMqttClient**.
4. Extract the SDK download. Choose **Sketch → Include Library → Add .ZIP
   Library…** and select the `EscapeDirector` ZIP inside it (not the download
   itself).

See [Arduino's library guide](https://support.arduino.cc/hc/en-us/articles/5145457742236-Install-libraries-in-the-Arduino-IDE)
if you can't find these menus. With Arduino CLI, see [Arduino CLI](#arduino-cli).

## 2. Upload the example

1. On a GIGA, attach its external Wi-Fi antenna.
2. Choose **File → Examples → EscapeDirector → room_basic**.
3. Select your board and its USB port (on a GIGA, the main M7 processor).
4. Click **Upload**. Keep the board connected by USB.

The example is a train door. The built-in LED stands in for the door relay, so
you need no wiring: touching pin **2** to **GND** (a button or a jumper wire)
pulls the lever and "opens" the door for five seconds.

## 3. Connect it to your Room

On the Room Station computer, start Room Connector, then in Escape Director:

1. Open the Room's **Devices** page, add an Integrated Device and choose
   **Your own firmware (SDK)**.
2. With the board on USB, choose **Pair controller**, then pick its Wi-Fi
   network. Room Connector sends the pairing over USB and checks the board
   connects.
3. Choose **Save props to Room**. **Train door** appears with its signal,
   commands and live state.

## 4. Try it

- **Test mode:** run **Open door**. The LED lights for one second.
- **Practice game:** link **Train door** to a Puzzle and start a game. Touch pin
  2 to GND: the LED lights for five seconds, the Room shows **Open** while it is
  lit, and the Puzzle completes.

## 5. Adapt your own sketch

Keep your puzzle logic, pins and relay polarity. Declare what Escape Director
should see beside the code that uses it:

```cpp
#include <EscapeDirectorRoom.h>

ed::Room room("Train door", "1.0.0");            // your sketch's name and version
ed::Prop &door = room.prop("door", "Train door"); // one per puzzle
ed::Input lever(2);                               // buttons and switches

void setup() {
  // ...your existing pin setup...
  door.signal("solved", "Lever pulled");      // what Escape Director hears
  door.command("open", "Open door", openDoor) // what the Game Master can do,
      .testPin(12, HIGH, 1000);               // and a safe one-second try in Test mode
  door.state("open", "Open", &doorOpen);      // live status
  room.onReset(closeDoor);
  room.begin();
}

void loop() {
  room.loop();
  if (lever.pressed()) { // your logic, unchanged
    openDoor();
    door.trigger("solved", lever);
  }
}
```

Then:

- Replace `delay()`: an output that must end on time becomes `room.pulse()`, a
  wait between steps becomes `room.after()`.
- Make buttons and switches `ed::Input`.
- Remove any `Serial.begin()` at another speed: USB serial is the setup channel.

The SDK handles Wi-Fi, pairing, reconnecting, IDs and Game and Test rules; there
is no JSON to write. The [ed::Room guide](docs/room-api.md) covers every call,
what pauses while the board reconnects, and a porting checklist. For a puzzle
that reads a sequence across several buttons, start from **button_code**.

After you change the sketch, change its version, upload it, then in the Device's
**Controller setup** choose **Check controller**, **Update connection** and
**Save props to Room**. Keep each prop's slug and each signal, command and state
ID: they are what the Room's links and Automations point to.

### With a coding agent

Put a copy of your sketch in the extracted download folder and open that folder
in your agent. `AGENTS.md` tells it how to integrate the SDK; `CLAUDE.md` points
to the same guide. Start with this prompt, replacing the brackets:

> Read AGENTS.md and integrate the Escape Director SDK into [sketch folder] for
> [exact board]. Preserve my existing puzzle logic, pins, output polarity and
> reset behavior. I want [what solves each prop] and [commands and state to show
> in Escape Director]. Inspect the code first and ask about anything the wiring
> or behavior does not establish. Compile for my board and tell me what changed,
> what you verified, and what still needs a physical test.

## Arduino CLI

From the extracted download (or this repository):

```sh
arduino-cli core update-index
arduino-cli core install arduino:renesas_uno@1.5.3
arduino-cli lib install ArduinoJson@7.4.3 ArduinoMqttClient@0.1.8
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi --library . examples/room_basic
```

For a GIGA, install `arduino:mbed_giga@4.6.0` and use
`--fqbn arduino:mbed_giga:giga`. Compiling does not upload; add `--upload -p <port>`
or use `arduino-cli upload`. On Windows, run the same commands in PowerShell.

## If the controller does not appear

Open the Device's **Controller setup**. If `room.begin()` rejected a declaration,
the reason is printed on USB serial (open the Serial Monitor at 115200 baud).
See [Troubleshooting](https://docs.escapedirector.com/build-your-rooms/build-your-own-controller#troubleshooting)
for other messages.
