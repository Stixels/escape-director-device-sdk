# Integrate with an AI coding agent

Open this repository (or the extracted download) with your coding agent. Put a
copy of your sketch under `workspace/` (ignored by Git) so the root `AGENTS.md`
applies, or ask the agent to read that file when the sketch lives elsewhere.
Never add Wi-Fi passwords, pairing records or customer data to the repository.

## Give the agent a concrete brief

> Read AGENTS.md and integrate the Escape Director SDK into workspace/my_prop
> for my [exact board]. Preserve my existing pins, output polarity, puzzle logic
> and reset behavior. The prop solves when [condition]. Expose [commands] and
> show [state] in Escape Director. Keep existing prop and capability IDs if
> present. Inspect the sketch first; ask about any wiring or behavior it does
> not establish. Compile for the board and report the exact result. Do not
> upload to my installed controller without my instruction.

The agent should declare your puzzles with `ed::Room` beside your existing
logic, not replace your sketch with an example or add its own networking.

## Verify before uploading

The agent compiles for the exact board with the commands in
[Getting started](../GETTING_STARTED.md#arduino-cli). `room.begin()` checks the
declarations on the board and prints any problem on USB serial at 115200 baud.

A compile pass does not prove correct wiring, timing or physical behavior. Ask
for a handoff with the changed files, preserved pins and IDs, library/core
versions, compile output and the remaining Test-mode and practice-game steps.

## Maintainer acceptance exercise

Before public release, test this path with a fresh agent that has only the
download and a synthetic existing sketch unlike `room_basic`. It should produce
a compiling adaptation that preserves pins, polarity and local reset/solve
behavior without private source or undocumented information. Record where the
guidance was insufficient and fix it. Bench verification is a separate check.
