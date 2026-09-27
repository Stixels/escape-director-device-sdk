# Add an Arduino board adapter

The public sketch interface is `EscapeDirector.h`: `ed::begin`, `ed::poll`,
`ed::startTimer`, `ed::captureGame` and `ed::signal`. Bundled adapters support
the Arduino UNO R4 WiFi and GIGA R1 WiFi. Other boards require an adapter;
choosing `architectures=*` in the library metadata does not mean every board is
supported or tested.

The library's portable helpers (`EscapeDirectorDevice.h` and the headers it
includes) have no Arduino dependencies. The shared Arduino client adds the USB
setup protocol, MQTT messages, discovery and Game/Test rules. `boards/UnoR4Board.cpp`
and `boards/GigaBoard.cpp` are complete reference adapters; the UNO's is the
better model for a board with little RAM. The managed firmware's configurable
puzzle engine is separate and is not part of the SDK.

## Before choosing a board

Confirm these facts in the board and core documentation:

| Requirement | What to find out                                                                                                                                                                                                                                                                  |
| ----------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Network     | Which Arduino library supplies Wi-Fi, a TLS `Client`, and a `UDP` socket? Can devices reach each other on the venue LAN?                                                                                                                                                          |
| TLS         | How do you install a private CA and verify the station identity? Does the client correctly verify IP addresses, or need the station's certificate name?                                                                                                                           |
| Storage     | What survives reset and power loss? What is the capacity, erase/page size and write endurance? Can a failed write preserve the last usable pairing?                                                                                                                               |
| USB         | Which serial object supports bidirectional setup, and what happens when USB opens or the board resets?                                                                                                                                                                            |
| Memory      | Does the board have RAM for the client on top of its network stack and your sketch? On the UNO R4 the whole example uses about 13 KB of static RAM and, once paired, 7 KB of heap; the heap high-water mark reaches about 15 KB while re-pairing. The description stays in flash. |
| Timing      | Can inputs and Test-output deadlines be serviced while network calls block? Which timer/task is suitable, and how is shared state protected?                                                                                                                                      |
| Electrical  | What are the GPIO voltage limits, usable pins and output polarity? Power outputs through appropriate drivers.                                                                                                                                                                     |

The current guided setup is USB plus Wi-Fi. An Ethernet-only board may speak the
wire protocol, but needs a suitable setup flow; this Wi-Fi wizard does not yet
provide one. Non-Arduino platforms can implement the documented MQTT contract
without using these Arduino libraries.

## Implement the platform boundary

Include `BoardAdapter.h` and derive from `ed::BoardAdapter`. Implement every
method in that header. Keep the adapter alive for the life of the sketch.

- `begin` initializes the USB stream and any required platform services.
- `setupStream` supplies newline-delimited JSON setup traffic at 115200 baud.
  Keep credentials and unrelated debug prints out of this stream.
- `secureClient` returns a persistent TLS-capable Arduino `Client`.
  `trustStation` configures the provided certificate; its backing string remains
  alive until reprovisioning. Never use an insecure TLS mode as a fallback.
- Network methods join/disconnect Wi-Fi, report connection state, and return the
  local IP and subnet mask. Network calls must finish within the app's setup
  deadlines; association without a DHCP address is not a usable connection.
- `scanNetworks` writes at most 32 `{ssid, rssi, secured}` objects, excluding empty
  SSIDs. Return false for a scan failure; a successful empty scan is different.
- `discoverySocket` returns a persistent Arduino `UDP`. The shared client binds
  and correlates discovery requests; the adapter does not implement discovery JSON.
- `loadPairing` fills the supplied document with the last valid saved record, or
  leaves it empty and returns false. `savePairing` must preserve the previous
  record on failure and confirm durable readback before returning true. Never
  print or commit the stored values. Both bundled adapters use two checksummed
  records; another board can use its own transactional storage instead. Stream
  the record to and from storage rather than buffering it, and check whether
  the storage API erases a whole block for each byte written (the UNO R4's
  `EEPROM.update` does).
- `startTimer` calls the given function every `periodMs` from a hardware timer
  that keeps running while network calls block. Return false if no timer is free.
- `id` returns a stable lowercase board identifier, such as `my-controller-v1`
  (letters, digits and hyphens; maximum 80 characters). Use the board's own ID;
  do not pretend it is a GIGA to pass identification.

The adapter must not contain MQTT topics, prop descriptions, signal rules or
Game/Test permissions. Those stay in the shared client.

## Connect it to a sketch

For a board with a bundled adapter, use:

```cpp
#include <EscapeDirector.h>

// DESCRIPTION, driver and sample follow the complete two_props example.
void setup() {
  // Configure this puzzle's own input/output pins first.
  ed::begin(DESCRIPTION, driver);
  ed::startTimer(sample, 5);
}
void loop() {
  ed::poll();
  // Service nonblocking puzzle work and deliver captured completion edges.
}
```

For your own adapter, the only SDK setup change is the third argument:

```cpp
#include <EscapeDirector.h>
#include "MyBoard.h"

MyBoard board;

void setup() {
  // Configure this puzzle's own pins first.
  ed::begin(DESCRIPTION, driver, board);
  ed::startTimer(sample, 5);
}
void loop() {
  ed::poll();
  // Service nonblocking puzzle work and captured completion edges.
}
```

These are integration excerpts, not complete standalone sketches. Start from the
bundled full example for the prop description, driver callbacks and edge handling.
Replace its LED constants and pin assignments with those for your board.
The puzzle `driver` implements state reporting, commands, Room reset and Test
cleanup. Its `testLease` deadline uses monotonic `millis()` time; the example
enforces it in the timer callback, so it holds even if `ed::poll()` is blocked
in networking. The adapter does not take ownership of your pins.

## Verify before offering the port

1. Compile a small description and confirm RAM/flash headroom. Compilation alone
   does not qualify a network stack, memory budget or physical output.
2. Pair over USB using **Your own firmware (SDK)**. Custom setup accepts a board ID
   with a valid custom description; managed firmware installers remain restricted
   to supported boards. Check scan, join, station verification and clear failure
   reporting without exposing credentials.
3. Verify the pinned CA accepts the correct station and rejects a different one.
4. Exercise state, commands and signals in Test mode and a disposable Game.
   Test effects must expire if networking blocks; expired or duplicate commands
   must not actuate outputs, and idle/Test signals must not complete Puzzles.
5. Reset and power-cycle the board. Retain pairing and repeat the authenticated
   handshake. Disconnect Wi-Fi and restart the Connector; never replay missed work.
6. Interrupt a pairing save and verify the last valid record survives. Test a
   larger description, scan failures and long-running clock synchronization.
7. Document the tested core/library versions, wiring, limits and recovery results.
