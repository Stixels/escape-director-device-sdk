# Contributing

Use synthetic examples and reproducible descriptions. Do not include customer
Rooms, credentials, pairing records or production logs. For account-specific
questions, use Escape Director support rather than an issue containing private data.

Read AGENTS.md and docs/compatibility.md before changing the API. Keep one outcome
per change, preserve stable IDs in examples, and document compatibility changes.
Source contributions are under Apache-2.0; retain applicable copyright notices.

## Validate

```sh
npm ci
npm run check
```

The tests require a C++17 compiler (`c++` by default; set `CXX` to a compatible
compiler executable). On Windows use a suitable Clang/MinGW environment; MSVC's
command-line flags are not supported by this runner. Firmware compilation uses
Arduino CLI and the pinned dependencies in GETTING_STARTED.md. Validate both
bundled board targets when shared code changes. Hardware findings must identify
the board, SDK/core versions, conditions and directly observed behavior.

## Prepare a download

From a clean, committed checkout with the `zip` command available:

```sh
npm run package -- /absolute/path/to/new-directory
```

The packager refuses to overwrite an existing directory and copies an explicit
allowlist into an Arduino library ZIP and a source/guide bundle. It records this
repository's commit and file hashes. Generated files belong outside the checkout
or in ignored `dist/`; never commit build output, installed dependencies or secrets.
Packaging is local and does not create a GitHub Release or publish an npm package.
The npm manifest is intentionally private: these are host tools for an Arduino
library, not a separately published JavaScript SDK.

A maintainer reviews licensing/dependencies, fresh-consumer checks and physical
qualification before approving a public release. Do not make the repository
public, create release tags, or upload release assets as a side effect of a test.
