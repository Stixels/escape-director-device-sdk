# Escape Director Device SDK

Bring your own Arduino prop code into Escape Director. Keep your puzzle logic;
the SDK connects its state, commands and completion signals to the Room.

**Private preparation candidate, version 0.3.0.** This repository is being
prepared for an open-source release under the MIT License. No public release is
available yet. Board adapters compile for UNO R4 WiFi and GIGA R1 WiFi; current
end-to-end hardware qualification remains incomplete. See
[compatibility and qualification](docs/compatibility.md).

## Start with your code

Use a copy of your sketch and follow [the getting-started guide](GETTING_STARTED.md).
The [two-prop example](examples/two_props/two_props.ino) is a complete reference
for the SDK callbacks, timer-based input handling and Test cleanup.

Working with an AI coding agent? Give it this repository and your sketch, then
use the [integration prompt and workflow](docs/agent-integration.md). The root
[AGENTS.md](AGENTS.md) is the agent's entry point; `CLAUDE.md` points to it too.
The instructions help the agent retain your pins, polarity, IDs and local puzzle
behavior instead of replacing your sketch with an example.

## What you need

- A board with an implemented adapter: UNO R4 WiFi or GIGA R1 WiFi for this
  candidate. Other boards need [a board adapter](BOARD_PORTING.md).
- Arduino IDE or Arduino CLI, ArduinoJson 7 and ArduinoMqttClient 0.1.8.
- Escape Director and Room Connector 0.8.5 for pairing and running the integration.
  The SDK source and host checks work without access to the application source;
  end-to-end operation still requires the product and Connector.

For software validation, install Node.js 24.18+ and a C++17 compiler:

```sh
npm ci
npm run check
npm run validate -- examples/two_props/description.json --header examples/two_props/Description.h
```

These commands validate the description, check its C++ copy and run the portable
SDK tests. For an agent-readable result:

```sh
node scripts/validate-description.mjs examples/two_props/description.json --json
```

To compile firmware, install the Arduino dependencies and use the commands in
[GETTING_STARTED.md](GETTING_STARTED.md). Compilation does not upload firmware.
The Node dependencies are development tools only; your controller does not run
Node or npm.

## Find what you need

| Task | Reference |
| --- | --- |
| Connect an existing sketch or upload the example | [Getting started](GETTING_STARTED.md) |
| Work with a coding agent | [Agent integration](docs/agent-integration.md) |
| Find callback signatures | [EscapeDirector.h](src/EscapeDirector.h) |
| Describe props, state, commands and signals | [Protocol](protocol.md) and [JSON Schema](description.schema.json) |
| Add a different board | [Board porting](BOARD_PORTING.md) and [BoardAdapter.h](src/BoardAdapter.h) |
| Check versions and support limits | [Compatibility](docs/compatibility.md) |
| Contribute or build a source download | [Contributing](CONTRIBUTING.md) |

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

## Private integration experiment

The [background-network prototype](experiments/README.md) tests keeping puzzle
logic in the normal Arduino loop without user timer or interrupt bookkeeping.
It is not the released API. See the [measured results](experiments/2026-09-28-results.md)
for the UNO trial and remaining memory/network qualification.
