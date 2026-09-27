# Integrate with an AI coding agent

Open this repository with your coding agent. Put a copy of your sketch under
`workspace/` (ignored by Git) so the root `AGENTS.md` applies, or explicitly ask
the agent to read that file when the sketch lives elsewhere. Never add Wi-Fi
passwords, pairing records or customer data to the repository.

## Give the agent a concrete brief

> Read AGENTS.md and integrate the Device SDK into workspace/my_prop for my
> [exact board]. Preserve my existing pins, output polarity, puzzle logic and
> reset behavior. The prop solves when [condition]. Expose [commands] and show
> [state] in Escape Director. Keep existing prop and capability IDs if present.
> Inspect the sketch first; ask about any wiring or behavior it does not establish.
> Validate the description, check its C++ copy, compile for the board and report
> the exact results. Do not upload to my installed controller without my instruction.

The SDK handles pairing and transport. The agent should adapt your state machine
to `CustomDriver`, not replace it with another MQTT client or rewrite a working
puzzle into the example's three-tap behavior.

## Verify before uploading

```sh
npm ci
npm run validate -- workspace/my_prop/description.json --header workspace/my_prop/Description.h
```

The validator checks schema, scope-unique IDs, completion references, number
ranges, enum labels and compact byte size. Optional `--state state.json` checks a
captured or synthetic state report against the description. `--json` returns
`{valid, errors, bytes}` (bytes may be absent for unreadable input); failure exits
with code 1. Diagnostics name fields rather than echoing rejected payload values.

`Description.h` validation expects the example's `DESCRIPTION[]` raw-string form.
For code that constructs a description differently, validate its exported JSON
and independently prove the actual firmware description matches it. Formatting
may differ, but JSON structure and field order must match for this check.

The agent then compiles for the exact board using the getting-started commands.
A compiler pass does not prove correct wiring, timing or physical behavior.
Ask for a handoff containing changed files, preserved pins/IDs, library/core
versions, compile output and the remaining Test-mode/practice-game steps.

## Maintainer acceptance exercise

Before public release, test this path with a fresh agent that has only this
repository and a synthetic existing sketch unlike `two_props`. It should produce
a compiling adaptation while preserving pins, polarity and local reset/solve
behavior without needing private source or undocumented information. Record
where guidance was insufficient and fix the repository. Human bench verification
is a separate check, after software validation.
