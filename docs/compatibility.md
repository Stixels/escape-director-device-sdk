# Compatibility

## Boards

| Board | Arduino core | FQBN |
| --- | --- | --- |
| Arduino UNO R4 WiFi | Arduino UNO R4 Boards 1.5.3 | `arduino:renesas_uno:unor4wifi` |
| Arduino GIGA R1 WiFi | Arduino Mbed OS Giga Boards 4.6.0 (main M7 processor) | `arduino:mbed_giga:giga` |

Other boards need an adapter; see [Add a board adapter](../BOARD_PORTING.md).

## Libraries

Install **ArduinoJson** 7 and **ArduinoMqttClient** 0.1.8 or later. The SDK is
tested with ArduinoJson 7.4.3 and ArduinoMqttClient 0.1.8.

You also need Escape Director and Room Connector on the Room Station. Keep Room
Connector up to date.

## Limits

- A Room has up to 8 props. A prop has up to 8 signals, 8 commands and 16 state
  fields.
- The default `ed::Room` reserves space for 48 signals, commands and state fields
  in total; `ed::BasicRoom<8, 64, 24>` reserves more.
- IDs use lowercase letters, digits and hyphens, start with a letter and have at
  most 40 characters. Names have 1–80 characters.

## Updating your firmware

Keep prop slugs (or the UUIDs you passed to `room.prop`) and every signal,
command and state ID when you update a sketch: the Room's links and Automations
point to them. Rename with names instead. If you remove a capability, check the
Room's links and Automations afterwards.

After uploading changed declarations, open the Device's **Controller setup**,
choose **Check controller**, then **Update connection**, and **Save props to
Room**.

## Your wiring and code

Wiring, loads and third-party libraries in your sketch remain your
responsibility. Code that blocks for a long time, such as `delay()`, needs the
changes described in the [ed::Room guide](room-api.md#porting-an-existing-sketch).

The SDK is for show control. It is not a safety controller: keep emergency
exits, safety interlocks and other life-safety functions independent of it.
