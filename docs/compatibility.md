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

CI compiles the packaged example for both listed targets on every push. The
portable tests cover command expiry/deduplication, authority, clock wrap, memory
helpers and pairing-slot fallback. These checks do not establish Wi-Fi/TLS
reliability, USB recovery or physical output behavior.

Before a public supported-board claim, complete current-version pairing, real
Wi-Fi/TLS/MQTT, Test mode, a practice game, reset, power-cycle and interruptions
on each claimed board/platform combination. GIGA evidence from older SDK versions
does not qualify this shared client. The UNO memory/storage checks do not replace
a real network-and-game check. macOS/Windows Connector qualification is separate.

## Compatibility policy

Keep prop UUIDs and capability IDs when updating compatible firmware. Rename
using labels. Explicitly document removed capabilities; saved Room links may
need repair. A changed description currently requires USB approval through
**Controller setup → Update firmware connection**, then **Save props to Room**.

During 0.x development, releases may introduce source-breaking changes; record
migration instructions and exact tested dependencies with each release. Do not
silently reuse a version or change a published download. A future contract
version must be negotiated explicitly, not presented as description v1.

Custom wiring, loads and third-party puzzle libraries remain the integrator's
responsibility. Arbitrary Arduino code may need nonblocking timing changes.
Non-Arduino platforms may implement the wire protocol, but do not gain a guided
setup flow or a supported adapter merely by speaking MQTT.

## Agent-consumer software exercise

A fresh agent adapted a synthetic three-button sequence using only the source
bundle and its guides. Description/header/state checks, the host suites and UNO
compilation passed; a separate host harness compared 1,118,480 sampled
input/reset transitions with the original local logic. This qualifies the
software-guidance path for that example only. No firmware was uploaded and no
physical or network behavior was observed. The exercise identified and prompted
corrections to the bundled example's Test-command flags and timer-start handling.
