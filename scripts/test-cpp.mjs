// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT
import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('../', import.meta.url));
const directory = mkdtempSync(join(tmpdir(), 'device-sdk-test-'));
try {
  for (const name of ['deviceSdkRuntime', 'stationClock', 'compactJson', 'compactAllocation', 'pairingSlots']) {
    const output = join(directory, name + (process.platform === 'win32' ? '.exe' : ''));
    execFileSync(process.env.CXX || 'c++', ['-std=c++17', '-Wall', '-Wextra', '-Isrc', `tests/${name}.cpp`, '-o', output], { cwd: root, stdio: 'inherit' });
    execFileSync(output, [], { stdio: 'inherit' });
    console.log(`${name} passed`);
  }
} finally {
  rmSync(directory, { recursive: true, force: true });
}
