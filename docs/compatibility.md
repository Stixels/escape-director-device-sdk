# Compatibility and qualification

This is a private preparation candidate for SDK 0.3.0 and description contract v1.
The source supports Arduino UNO R4 WiFi and GIGA R1 WiFi adapters. Adapter presence
and compilation are not promises of qualified operation on every board/network.

| Component | Candidate baseline |
| --- | --- |
| Escape Director Device SDK | 0.3.0 |
| Custom description contract | 1; compact UTF-8 description at most 16 KiB |
| Room Connector | 0.8.5 |
| UNO core | Arduino Renesas UNO Boards 1.5.3 |
| GIGA core | Arduino Mbed OS Giga Boards 4.6.0; M7 target |
| ArduinoJson | 7.4.3 |
| ArduinoMqttClient | 0.1.8 |
| Host tooling | Node.js 24.18+, C++17; `zip` for local packaging |

CI compiles both examples for both listed targets on every push. The host tests cover the `ed::Room` runtime (inputs, press queue, pulses,
Test pins, declarations), the generated description, command expiry and
deduplication, clock wrap, station address choice, memory helpers and
pairing-slot fallback. These checks do not establish Wi-Fi/TLS
reliability, USB recovery or physical output behavior.

Before a public supported-board claim, complete current-version pairing, real
Wi-Fi/TLS/MQTT, Test mode, a practice game, reset, power-cycle and interruptions
on each claimed board/platform combination. GIGA evidence from older SDK versions
does not qualify this shared client. The UNO memory/storage checks do not replace
a real network-and-game check. macOS/Windows Connector qualification is separate.

## Compatibility policy

Keep prop slugs (or explicit UUIDs) and capability IDs when updating firmware.
Rename using names. Explicitly document removed capabilities; saved Room links
may need repair. Changed declarations require **Controller setup → Check
controller → Update connection** over USB, then **Save props to Room**.

During 0.x development, releases may introduce source-breaking changes; record
migration instructions and exact tested dependencies with each release. Do not
silently reuse a version or change a published download. A future contract
version must be negotiated explicitly, not presented as description v1.

Custom wiring, loads and third-party puzzle libraries remain the integrator's
responsibility. Arbitrary Arduino code may need nonblocking timing changes.
Non-Arduino platforms may implement the wire protocol, but do not gain a guided
setup flow or a supported adapter merely by speaking MQTT.

## Agent-consumer software exercise

An earlier fresh-agent exercise used the removed lower-level API. Repeat it with
`ed::Room` before a public release (see [agent integration](agent-integration.md)).
