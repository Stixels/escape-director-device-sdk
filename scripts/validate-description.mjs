// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import Ajv2020 from 'ajv/dist/2020.js';
import addFormats from 'ajv-formats';

const schema = JSON.parse(readFileSync(new URL('../description.schema.json', import.meta.url), 'utf8'));
const ajv = new Ajv2020({ allErrors: true });
addFormats(ajv);
const checkSchema = ajv.compile(schema);
const issue = (path, message) => ({ path, message });

export function validateDescription(description) {
  const errors = [];
  const bytes = Buffer.byteLength(JSON.stringify(description), 'utf8');
  if (!checkSchema(description)) {
    errors.push(...checkSchema.errors.map(e => issue(e.instancePath || '/', e.message)));
    return { valid: false, bytes, errors };
  }
  if (bytes > 16384) errors.push(issue('/', 'Compact description exceeds 16384 bytes'));
  const unique = (values, path) => {
    if (new Set(values).size !== values.length) errors.push(issue(path, 'Identifiers must be unique within their scope'));
  };
  const label = (value, path) => {
    if (typeof value === 'string' && !value.trim()) errors.push(issue(path, 'Text must contain a non-whitespace character'));
  };
  label(description.name, '/name');
  unique(description.props.map(p => p.id), '/props');
  description.props.forEach((prop, index) => {
    const path = `/props/${index}`;
    label(prop.name, `${path}/name`);
    for (const kind of ['signals', 'commands', 'state']) {
      unique(prop[kind].map(c => c.id), `${path}/${kind}`);
      prop[kind].forEach((c, i) => {
        label(c.name, `${path}/${kind}/${i}/name`);
        label(c.description, `${path}/${kind}/${i}/description`);
      });
    }
    if (prop.completion && (!prop.signals.some(c => c.id === prop.completion.signal) ||
        !prop.commands.some(c => c.id === prop.completion.command))) {
      errors.push(issue(`${path}/completion`, 'Completion must reference a declared signal and command'));
    }
    prop.state.forEach((field, index) => {
      const fieldPath = `${path}/state/${index}`;
      if (field.type === 'number') {
        if (field.min > field.max) errors.push(issue(fieldPath, 'Invalid numeric range'));
        label(field.unit, `${fieldPath}/unit`);
      }
      if (field.type === 'enum') {
        unique(field.values, `${fieldPath}/values`);
        for (const [key, value] of Object.entries(field.labels ?? {})) {
          if (!field.values.includes(key)) errors.push(issue(`${fieldPath}/labels`, 'Labels must refer to declared enum values'));
          label(value, `${fieldPath}/labels`);
        }
      }
    });
  });
  return { valid: errors.length === 0, bytes, errors };
}

export function validateState(description, report) {
  const errors = [];
  const object = v => v !== null && typeof v === 'object' && !Array.isArray(v);
  if (!object(report) || Object.keys(report).length !== 1 || !Array.isArray(report.props)) {
    return { valid: false, errors: [issue('/', 'Expected only a props array')] };
  }
  const seen = new Set();
  if (report.props.length !== description.props.length) errors.push(issue('/props', 'Report every declared prop exactly once'));
  report.props.forEach((entry, index) => {
    const path = `/props/${index}`;
    if (!object(entry) || Object.keys(entry).length !== 2 || !object(entry.values) || typeof entry.id !== 'string') {
      errors.push(issue(path, 'Expected id and values only'));
      return;
    }
    const declared = description.props.find(p => p.id === entry.id);
    if (!declared || seen.has(entry.id)) {
      errors.push(issue(path, 'Unknown or duplicate prop'));
      return;
    }
    seen.add(entry.id);
    if (Object.keys(entry.values).length !== declared.state.length) errors.push(issue(`${path}/values`, 'Report exactly the declared fields'));
    for (const field of declared.state) {
      const value = entry.values[field.id];
      const valid = Object.hasOwn(entry.values, field.id) && (field.type === 'boolean' ? typeof value === 'boolean' :
        field.type === 'number' ? typeof value === 'number' && Number.isFinite(value) && value >= field.min && value <= field.max :
        typeof value === 'string' && field.values.includes(value));
      if (!valid) errors.push(issue(`${path}/values/${field.id}`, 'Value does not match declared type or bounds'));
    }
  });
  return { valid: errors.length === 0, errors };
}

export function validateHeader(description, text) {
  const literal = text.match(/\bDESCRIPTION\s*\[\s*\]\s*=\s*R"([A-Za-z0-9_]{0,16})\(([\s\S]*?)\)\1"\s*;/);
  if (!literal) return [issue('/header', 'Expected DESCRIPTION[] as a C++ raw JSON string, as in the example')];
  try {
    if (JSON.stringify(JSON.parse(literal[2])) !== JSON.stringify(description)) {
      return [issue('/header', 'DESCRIPTION and description.json differ')];
    }
  } catch {
    return [issue('/header', 'DESCRIPTION is not valid JSON')];
  }
  return [];
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  const json = args.includes('--json');
  let result;
  try {
    const file = args.shift();
    if (!file || file.startsWith('--')) throw new Error('Usage: node scripts/validate-description.mjs description.json [--header Description.h] [--state state.json] [--json]');
    const options = {};
    while (args.length) {
      const key = args.shift();
      if (key === '--json') continue;
      if (!['--header', '--state'].includes(key) || !args.length || args[0].startsWith('--')) throw new Error('Invalid command options');
      options[key] = args.shift();
    }
    const description = JSON.parse(readFileSync(file, 'utf8'));
    result = validateDescription(description);
    if (options['--header']) result.errors.push(...validateHeader(description, readFileSync(options['--header'], 'utf8')));
    if (result.valid && options['--state']) result.errors.push(...validateState(description, JSON.parse(readFileSync(options['--state'], 'utf8'))).errors);
    result.valid = result.errors.length === 0;
  } catch (error) {
    result = { valid: false, errors: [issue('/', error instanceof SyntaxError ? 'Invalid JSON input' : error.message)] };
  }
  console.log(json ? JSON.stringify(result) : result.valid ? `Valid description (${result.bytes} compact UTF-8 bytes)` : result.errors.map(e => `${e.path}: ${e.message}`).join('\n'));
  process.exitCode = result.valid ? 0 : 1;
}
