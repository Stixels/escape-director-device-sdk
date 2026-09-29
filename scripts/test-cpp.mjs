// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT
import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import { homedir, tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('../', import.meta.url));
const directory = mkdtempSync(join(tmpdir(), 'device-sdk-test-'));
// roomApi runs the real ed::Room code against tests/host stand-ins for the
// Arduino core. It needs ArduinoJson's headers (the version in
// docs/compatibility.md): set ARDUINOJSON_SRC or install it with the Arduino IDE
// or `arduino-cli lib install ArduinoJson@7.4.3`.
const arduinoJson = [process.env.ARDUINOJSON_SRC,
  ...['Documents/Arduino', 'Arduino'].map(d => join(homedir(), d, 'libraries/ArduinoJson/src'))]
  .find(d => d && existsSync(join(d, 'ArduinoJson.h')));
if (!arduinoJson) throw new Error('ArduinoJson not found: set ARDUINOJSON_SRC to its src directory');
const roomApiFlags = ['-DARDUINO_UNOR4_WIFI', '-Itests/host', `-I${arduinoJson}`,
  ...(process.platform === 'win32' ? [] : ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g'])];
try {
  for (const name of ['commandWindow', 'stationClock', 'compactAllocation', 'pairingSlots', 'roomRegistry', 'stationAddress', 'roomApi']) {
    const output = join(directory, name + (process.platform === 'win32' ? '.exe' : ''));
    const flags = name === 'roomApi' ? roomApiFlags : [];
    execFileSync(process.env.CXX || 'c++', ['-std=c++17', '-Wall', '-Wextra', ...flags, '-Isrc', `tests/${name}.cpp`, '-o', output], { cwd: root, stdio: 'inherit' });
    // roomRegistry also writes the description it generates, which must satisfy the schema.
    const generated = join(directory, `${name}.json`);
    execFileSync(output, name === 'roomRegistry' ? [generated] : [], { stdio: 'inherit' });
    if (name === 'roomRegistry')
      execFileSync(process.execPath, ['scripts/validate-description.mjs', generated], { cwd: root, stdio: 'inherit' });
    console.log(`${name} passed`);
  }
} finally {
  rmSync(directory, { recursive: true, force: true });
}
