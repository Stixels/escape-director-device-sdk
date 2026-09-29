# Escape Director Device SDK

Bring your own Arduino prop code into Escape Director. Keep your puzzle logic;
the SDK connects its state, commands and completion signals to the Room.

**Private preparation candidate, version 0.3.0.** This repository is being
prepared for an open-source release under the MIT License. No public release is
available yet. Board adapters compile for UNO R4 WiFi and GIGA R1 WiFi; current
end-to-end hardware qualification remains incomplete. See
[compatibility and qualification](docs/compatibility.md).

## Start with your code

Follow [the getting-started guide](GETTING_STARTED.md): it uploads the
[room_basic example](examples/room_basic/room_basic.ino), connects it to a Room
and shows how to adapt your own sketch. You declare props, signals, commands and
state beside your code with `ed::Room`, read buttons with `ed::Input` and time
outputs with `room.pulse()`; the SDK handles Wi-Fi, pairing, reconnection, Game
and Test rules. The [ed::Room guide](docs/room-api.md) covers every call.

Working with an AI coding agent? Give it the download and your sketch, then use
the [integration prompt](docs/agent-integration.md). The root
[AGENTS.md](AGENTS.md) is the agent's entry point; `CLAUDE.md` points to it too.

## What you need

- A board with an adapter: UNO R4 WiFi or GIGA R1 WiFi. Other boards need
  [a board adapter](BOARD_PORTING.md).
- Arduino IDE or Arduino CLI, ArduinoJson 7 and ArduinoMqttClient 0.1.8.
- Escape Director and Room Connector for pairing and running the integration.

To change the SDK itself, install Node.js 24.18+, a C++17 compiler and
ArduinoJson, then run `npm ci` and `npm run check`. The Node tools are for
development only; your controller does not run Node.

## Find what you need

| Task | Reference |
| --- | --- |
| Upload the example or connect your sketch | [Getting started](GETTING_STARTED.md) |
| Every call, outage behavior and a porting checklist | [ed::Room guide](docs/room-api.md) and [EscapeDirectorRoom.h](src/EscapeDirectorRoom.h) |
| Work with a coding agent | [Agent integration](docs/agent-integration.md) |
| Add a different board | [Board porting](BOARD_PORTING.md) and [BoardAdapter.h](src/BoardAdapter.h) |
| Check versions and support limits | [Compatibility](docs/compatibility.md) |
| Wire protocol (SDK maintainers) | [Protocol](protocol.md) |
| Contribute or build a download | [Contributing](CONTRIBUTING.md) |

The SDK is for show control. It is not a safety controller; keep emergency exits,
safety interlocks and other life-safety functions independent. Test on a spare
board with prop loads disconnected before adapting an installed controller.

## License

SDK code, examples, documentation and local tooling are licensed under the
[MIT License](LICENSE): use, modify and redistribute them however you like,
including in commercial props, as long as the license notice stays with the
source. [Escape Director trademarks](TRADEMARKS.md) remain reserved.
Separately installed libraries and board cores retain their own licenses; see
[third-party dependencies](THIRD_PARTY.md).
