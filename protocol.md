# Custom Device MQTT v1

This is the custom transport contract used by Room Connector, the app pairing
flow and the shared Arduino SDK client. Existing managed MQTT v1 stays unchanged.
Sketches that use the SDK libraries only need the description reference below;
the client handles the wire messages.

## Description reference

A description is JSON of at most 16 KiB (16384 bytes of compact UTF-8), so it
fits one setup or MQTT message. Unknown fields, unknown versions and duplicate
IDs are rejected. An **ID** is 1–40 lowercase letters, digits and hyphens,
starting with a letter. A **name** is 1–80 characters and can be renamed freely.
`description.schema.json` checks the structure; the size limit, unique IDs and
cross-references are checked when the controller pairs.

| Field                     | Rule                                                           |
| ------------------------- | -------------------------------------------------------------- |
| `contractVersion`         | `1`                                                            |
| `firmwareVersion`         | 1–40 characters                                                |
| `name`                    | Name shown for the firmware                                    |
| `configuration`           | `"firmware-owned"`                                             |
| `diagnostics.transitions` | `false` (raw input transitions are not supported yet)          |
| `props`                   | 1–8 props                                                      |
| `props[].id`              | UUID, such as a random v4; keep it stable                      |
| `props[].name`            | Name                                                           |
| `props[].signals`         | Up to 8 `{ id, name, description? }`                           |
| `props[].commands`        | Up to 8 `{ id, name, testable, description? }`                 |
| `props[].state`           | Up to 16 fields; see below                                     |
| `props[].completion`      | Optional `{ signal, command }`; see Optional Puzzle completion |

Each state field has an `id`, `name`, optional `description` and a `type`:

| `type`    | Extra fields                                    | Reported value                      |
| --------- | ----------------------------------------------- | ----------------------------------- |
| `boolean` | —                                               | `true` or `false`                   |
| `number`  | `min`, `max`, optional `unit` (1–16 characters) | A finite number from `min` to `max` |
| `enum`    | `values` (1–16 IDs), optional `labels`          | One of `values`                     |

`description` is 1–240 characters. `labels` maps each value to a name; values
without a label show as written. The SDK streams the description from flash,
so its size does not use board RAM.

## Identity and negotiation

A custom credential uses the same station CA, controller UUID/password and
`ed/v1/<controllerId>/up|down` topics as managed firmware. The Connector explicitly
provisions it with a SHA-256 `descriptionId`; a managed credential cannot announce
itself as custom. Different-controller replacement remains a future flow. Same-controller description
updates require the explicit USB Update firmware connection handshake; they never overwrite
credentials automatically.

The fingerprint hashes the validated description with object keys sorted in
lexical order, arrays retained in declared order, and compact JSON encoding.
Labels are included. The Node reference computes it with `descriptionId()`;
a board can echo the provisioned fingerprint after its description is accepted.

1. After authenticated subscription, the Connector sends its ordinary `welcome`
   plus `customProtocolVersion: 1` and `descriptionId`. It includes a fresh
   `sessionId`, the provisioned `authorityVersion`, and station `now` in epoch ms.
2. The peer validates authority and sends `custom-hello` with `protocolVersion: 1`,
   the session ID and its full `description`.
3. The Connector validates the description and fingerprint, then replies with
   `custom-ready`, the session ID and fingerprint. A changed description closes
   the connection. Unsupported versions never become operational.
4. The peer sends `custom-state` containing the same session ID, fingerprint and
   complete `state`. Only a validated state enables requests. Report every 500 ms;
   the Connector closes peers without valid state for eight seconds. MQTT keepalive
   is five seconds. A description alone does not make a controller operational.

## Stored pairing data

After pairing, the bundled adapters save the Wi-Fi SSID and password, the
station CA certificate and the controller UUID/password in the board's
persistent storage (UNO R4 data flash; GIGA key-value store). This data is not
encrypted: anyone with physical or USB access to the board can read it. Keep
controllers in locked or staff-only areas. Before reusing, selling or
discarding a board, erase its storage with a sketch that clears it; pairing
again is not enough, because the adapters keep the previous record in a second
slot as a fallback. If a paired board
leaves your control, pair a replacement controller for that Device, which
revokes the old controller credential, and treat the saved Wi-Fi password as
exposed. A custom `BoardAdapter` decides its own storage; apply the same
precautions.

## Message rules

Credentials, certificates and network passwords never belong in descriptions or
state. Messages are bounded to 32 KiB. Operational messages use QoS 0, retain false,
clean sessions and no offline publish queue. Reconnection repeats negotiation and
begins idle; the peer never restores previous Game or Test authority itself.
A still-active Room Dashboard may automatically request a fresh `start` for the
same previously admitted controller after identity and configuration checks.
That establishes new wire authority without replaying missed events/commands or
restoring output state. Test mode always requires explicit user entry.

## Requests and results

`custom-request` contains `requestId`, `sessionId`, `authorityVersion`,
`descriptionId`, station-clock `expiresAt` and an `operation`:

| Operation      | Fields                                        | Rule                                                               |
| -------------- | --------------------------------------------- | ------------------------------------------------------------------ |
| `start`        | `gameId`                                      | Enter Game from idle.                                              |
| `end`          | `gameId`                                      | Exit that Game without resetting local puzzle state.               |
| `test-mode`    | `testId`, `enabled`                           | Enter from idle or exit that Test session.                         |
| `lease`        | nullable `gameId`, nullable `testId`          | Renew only the exact active context.                               |
| `command`      | `commandId`, `gameId`, `propId`, `capability` | Invoke a declared command in that Game.                            |
| `test-command` | `commandId`, `testId`, `propId`, `capability` | Invoke a declared `testable` command in that Test session.         |
| `reset-room`   | none                                          | Explicit idle-only reset; observation-only drivers may do nothing. |

Expiry must be in the future and at most three seconds away. Requests and signals
use the station clock; board-local deadlines must use a monotonic clock. Authority
expires at its lease deadline even if another operation arrives before a timer
callback. Expiry and disconnect remove temporary Test effects but preserve ordinary
local puzzle state. Driver callbacks return promptly; timed effects belong in the
local loop or board timer.

A `result` echoes the session, fingerprint and request ID, with `outcome` equal to
`completed` or `rejected`, and an optional bounded `code`. Send the result before
state telemetry. A completed result confirms dispatch; it is not evidence that a
physical latch moved. Report actual driver state separately.

Keep at most 64 unexpired command identities. For command operations use
`commandId`; for other operations use `requestId`. Lease renewals do not consume
slots. Consume the identity before calling the driver, including failed dispatch.
A duplicate returns `confirmation_unknown` and never executes again; a full window
returns `command_limit`. Never retry an uncertain actuation after reconnect.

## Station clock synchronization

SDK 0.1.1 and later refresh the station clock every ten seconds through the authenticated
MQTT connection (requires Room Connector 0.8.1 or newer). Send `clock-sync` with
only `sessionId` and a positive uint32 `sequence`. The reply echoes these fields
and adds `authorityVersion` and station `now` in epoch milliseconds. These
transport messages do not carry a description fingerprint or renew control leases.

The portable `StationClock` accepts only the current pending sequence, with a
round trip of at most 500 ms; the client also checks session and authority.
Late, duplicate and retained replies are ignored. Reconnect establishes a fresh
anchor from `welcome`. Command and signal expiry remain unchanged; local I/O
and lease deadlines continue using monotonic board time. Older Connectors can
ignore the requests but do not provide the long-connection drift correction.

## State and signals

`custom-state` reports every advertised prop exactly once, with exactly its declared
boolean, bounded number or enum fields. There are no managed input arrays, pin
assignments or configuration versions. A malformed report neither replaces state
nor extends the state timeout.

`custom-signal` contains the session and description IDs, a fresh `eventId`, current
`gameId`, declared `propId` and signal `capability`, plus `occurredAt` and `expiresAt`.
Signals are live edges, never inferred from a state snapshot. Emit only during
current Game authority; Test and idle observations remain state only. Expiry is no
more than three seconds after occurrence. The Connector rejects stale timestamps,
unknown capabilities and signals before the first valid state.

The app profile must match Game ownership and the admitted description,
deduplicate event IDs, and keep Test/diagnostic events out of Automations and Session
Logs. Transport validation alone is not browser/Game admission. Raw transition
batches are not implemented for custom firmware yet; the example declares no
transition support.

## Optional Puzzle completion

A prop may declare `"completion": { "signal": "completed", "command": "complete" }`.
Both keys must reference capabilities declared on that prop. The command must
complete the local prop using its ordinary output behavior and emit that signal
only on a new completion during a Game. Repeating it while solved must not produce
another completion signal. Test commands remain local; reset is a separate action.

Room Connector 0.7.2 or newer accepts this metadata. It participates in the
existing description fingerprint, so adding or changing it requires Update firmware connection
and Save props to Room. Existing descriptions without completion remain valid and
usable with Automations. The Room owns the selected Puzzle; firmware never stores
Puzzle IDs or application gameplay relationships.

## Labels and guidance

SDK 0.2.0 with Room Connector 0.8.2 supports optional presentation metadata:

- Signals, commands and state fields may include `description` (1–240 characters).
- Number state fields may include `unit` (1–16 characters), for example `cm`.
- Enum state fields may include `labels`, mapping declared values to display names
  (1–80 characters each). Omitted labels fall back to the raw enum value.

```json
{
  "id": "position",
  "name": "Door",
  "type": "enum",
  "values": ["door-open", "door-closed"],
  "labels": { "door-open": "Open", "door-closed": "Closed" },
  "description": "Reported by the door contact."
}
```

These are plain text, not formatting, conversion or behavioral instructions.
They participate in the description fingerprint. Keep state values and capability
IDs stable when changing labels. Existing descriptions may omit every new field;
older strict validators require a Connector/app update before accepting metadata.

A rejected authenticated announcement exposes a bounded description problem to
the app. It distinguishes `description_conflict` (use **Update firmware connection**)
from `invalid_description` (fix the listed fields). It never admits rejected props
or supplies rejected values as state. Field details contain schema guidance, not
the rejected payload. The last problem survives a browser refresh until a valid
announcement is accepted; it is not persisted across a Connector restart.
