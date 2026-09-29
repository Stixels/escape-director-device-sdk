# Contributing

Use synthetic examples and reproducible descriptions. Do not include customer
Rooms, credentials, pairing records or production logs. For account-specific
questions, use Escape Director support rather than an issue containing private data.

Read AGENTS.md and docs/compatibility.md before changing the API. Keep one outcome
per change, preserve stable IDs in examples, and document compatibility changes.
Contributions are accepted under the same MIT License as the rest of the
repository (inbound = outbound). By opening a pull request you confirm that you
wrote the change or otherwise have the right to submit it under that license.
Keep the existing copyright and SPDX headers; new source files use
`// SPDX-License-Identifier: MIT`.

## Validate

```sh
npm ci
npm run check
```

The tests require a C++17 compiler (`c++` by default; set `CXX` to a compatible
compiler executable). On Windows use a suitable Clang/MinGW environment; MSVC's
command-line flags are not supported by this runner. Firmware compilation uses
Arduino CLI and the pinned dependencies in GETTING_STARTED.md. `npm test` also
needs ArduinoJson's headers (installed with the Arduino IDE, or set
`ARDUINOJSON_SRC`). CI compiles both examples for both bundled boards; run the
same commands locally when shared code changes. When you report a hardware
finding, name the board, the SDK and core versions, the conditions and what you
observed.

## Prepare a download

From a clean, committed checkout with the `zip` command available:

```sh
npm run package -- /absolute/path/to/new-directory
```

The packager refuses to overwrite an existing directory and copies an explicit
allowlist into an Arduino library ZIP and a source/guide bundle. It records this
repository's commit and file hashes. Generated files belong outside the checkout
or in ignored `dist/`; never commit build output, installed dependencies or secrets.
Packaging is local and does not create a GitHub Release. The npm manifest is
marked private because its packages are development tools for an Arduino
library; nothing is published to npm.
