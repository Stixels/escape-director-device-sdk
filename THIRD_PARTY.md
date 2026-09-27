# Third-party dependencies

The source download does not vendor the dependencies below. Install them from
their upstream distributions and retain their license notices. The SDK's
MIT License does not change those dependencies' terms.

| Dependency | Baseline | License information |
| --- | --- | --- |
| ArduinoJson | 7.4.3 | MIT, as supplied in its LICENSE.txt |
| ArduinoMqttClient | 0.1.8 | LGPL-2.1 family; consult its LICENSE.txt and source headers |
| Arduino Renesas UNO core | 1.5.3 | Core repository LICENSE is MIT; bundled HAL/network/RTOS components carry their own notices |
| Arduino Mbed OS Giga core | 4.6.0 | Includes LGPL-2.1-or-later Arduino/SocketWrapper code and separately licensed Mbed components; consult each component's notices |
| Ajv (host validator) | Locked in package-lock.json | MIT |
| ajv-formats (host validator) | Locked in package-lock.json | MIT |

The board cores supply networking, TLS, USB, timers and persistent storage.
Review their complete resolved components before distributing compiled firmware;
a source package and an executable firmware image have different contents.
This repository's package contains source only, not those core binaries or
third-party libraries. The host dependency lockfile also identifies transitive
validator dependencies; npm installs their notices with each package.

Upstream references:

- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
- [ArduinoMqttClient](https://github.com/arduino-libraries/ArduinoMqttClient)
- [ArduinoCore-renesas](https://github.com/arduino/ArduinoCore-renesas)
- [ArduinoCore-mbed](https://github.com/arduino/ArduinoCore-mbed)
- [Ajv](https://github.com/ajv-validator/ajv)
- [ajv-formats](https://github.com/ajv-validator/ajv-formats)
