# Background-network prototype (private)

This branch tests an integration model, not a new supported SDK release.
The existing example/API stays available. Prototype headers and sketches are
excluded from the package allowlist. Do not publish this branch as a release.

## Integrator shape

`background_two_props` preserves the existing D2/D3 button logic and board LED
outputs. Its normal Arduino loop is simply:

```cpp
void loop() {
  connection.poll();
  sample();
}
```

Initialization is `connection.begin(DESCRIPTION)`, with its return value checked.
Completion is `connection.signal(propId, "completed")`. Implement four callbacks:
`state`, `command`, `resetRoom`, and `endTest`. They all execute on the sketch
thread. There is no user timer, interrupt lock, lease callback or manual edge
transfer. A false signal return means offline or the bounded queue is full;
true means locally queued, never confirmed delivery.

Sketch functions still need to return promptly. The SDK cannot make arbitrary
blocking sensor reads or user `delay()` calls responsive. Temporary Test cleanup
runs from `connection.poll()` independently of network progress; it is not an
independent hardware safety cutoff. Existing example pulse duration remains in
its ordinary local state machine. A general timed-output helper is not implemented
by this prototype.

## Execution and ownership

Only the network worker calls the original protocol client's `ed::poll()` and
`ed::signal()`, and owns Wi-Fi, TLS, MQTT, provisioning, credentials and USB setup.
The sketch must not call WiFi/MQTT or the original ed::poll/ed::signal alongside
this wrapper. Do not simultaneously write Serial diagnostics while the worker
uses the provisioning stream; this needs a serialized logging facility before
publication. The SDK remains single-controller, once-per-setup initialization.

The worker uses a one-request rendezvous for callbacks. It retains argument
storage until the sketch acknowledges completion; it waits without locking the
sketch. Commands and Room reset are checked again against their monotonic expiry
immediately before the sketch executes them. Completion events use a four-entry
single-producer/single-consumer queue, retaining edge time/game generation. The
existing protocol client rechecks age, generation and connection before sending.
No events are persisted or retried for a later game.

UNO R4 uses the Arduino core's FreeRTOS library. Its default scheduler disables
time slicing: the networking worker therefore runs below the sketch's priority,
and `connection.poll()` yields one scheduler tick. Equal priorities would be
wrong for busy modem waits. The normal core sketch stack is 4096 bytes; the
prototype network stack is 2048 bytes. A conservative heap reservation rejects
startup if there is insufficient room for core sketch, idle and timer tasks.
Newlib malloc hooks serialize heap metadata only after the scheduler starts.
This does not establish arbitrary third-party library thread safety.

GIGA uses an Mbed thread below the main task's priority with a 4096-byte stack.
The same one-millisecond scheduling pause gives it execution time. No networking
or user driver callback is moved into an interrupt.

## Software checks

```sh
npm run check
npm run test:background
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi --library . experiments/background_two_props
arduino-cli compile --fqbn arduino:mbed_giga:giga --library . experiments/background_two_props
```

The prototype host test exercises 200,000 queue transfers and 10,000 callback
handoffs across real host threads, saturation, one-shot request consumption,
deadline wrap, local sampling and output expiry during a busy network stall,
and rejection of an expired command after that stall. It does not emulate the
Arduino scheduler, network drivers, memory pressure or actual hardware timing.
CI also compiles both the original and prototype examples on both pinned cores.

## Required board qualification before adopting this API

- Check actual task startup; measure both task stack high-water marks and peak
  general/RTOS heap usage through pairing with a full certificate/description.
  Static compile memory figures do not establish runtime headroom.
- Observe loop latency on the board while joining unavailable Wi-Fi, waiting for
  DHCP, connecting TLS/MQTT, sending state and losing Room Connector. Exercise
  physical button edges and outputs throughout; establish an acceptable bound.
- Test a temporary output through loss of network, lease expiry and reconnect.
  Confirm cleanup preserves ordinary solved state and no old command/event is
  replayed. A hardware watchdog/timed-output helper remains separate work.
- Check serial provisioning and reset/replacement paths under task scheduling.
- Test simultaneous sketch/library allocation and input buses; preserve one owner
  per peripheral. Verify the malloc hooks are present in the UNO linked image.
- Re-run the clean integrator exercise, including an I2C/SPI sensor sketch, before
  turning this experiment into the default SDK API.

No physical qualification or upload is implied by compilation or host tests.
The repository remains private until the maintainer approves publication.

Measured software and UNO results are recorded in [the dated bench report](2026-09-28-results.md).
