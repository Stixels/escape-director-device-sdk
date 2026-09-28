// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT
import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
const directory = mkdtempSync(join(tmpdir(), 'ed-background-'));
try {
  const binary = join(directory, 'test');
  execFileSync(process.env.CXX || 'c++', ['-std=c++17', '-pthread', '-Wall', '-Wextra', '-Isrc', 'tests/backgroundMailbox.cpp', '-o', binary], { stdio: 'inherit' });
  execFileSync(binary, [], { stdio: 'inherit' });
} finally { rmSync(directory, { recursive: true, force: true }); }
