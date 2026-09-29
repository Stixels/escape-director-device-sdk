// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import test from 'node:test';
import { validateDescription, validateState } from '../scripts/validate-description.mjs';
const example = JSON.parse(readFileSync(new URL('./fixtures/description.json', import.meta.url), 'utf8'));
const copy = () => structuredClone(example);

test('the fixture description is valid', () => {
  assert.equal(validateDescription(example).valid, true);
});
for (const [name, change] of [
  ['duplicate props', d => d.props.push(structuredClone(d.props[0]))],
  ['duplicate commands', d => d.props[0].commands.push(structuredClone(d.props[0].commands[0]))],
  ['unknown fields', d => d.credentials = 'must-not-be-here'],
  ['missing completion reference', d => d.props[0].completion.signal = 'missing'],
  ['reversed range', d => d.props[0].state.push({id:'count', name:'Count', type:'number', min:5, max:1})],
  ['duplicate enum values', d => d.props[0].state.push({id:'mode', name:'Mode', type:'enum', values:['idle','idle']})],
  ['unknown enum label', d => d.props[0].state.push({id:'mode', name:'Mode', type:'enum', values:['idle'], labels:{missing:'Missing'}})],
  ['blank label', d => d.props[0].name = '   '],
]) test(`rejects ${name}`, () => { const d=copy();change(d);assert.equal(validateDescription(d).valid,false); });

test('UTF-8 size limit applies even within field/count limits', () => {
  const d=copy();
  d.props=Array.from({length:8},(_,i)=>({id:`0210a18f-c31f-45b7-a35a-e20eb82a6c1${i}`,name:'Prop', signals:Array.from({length:8},(_,j)=>({id:`signal-${j}`,name:'Signal',description:'é'.repeat(240)})),commands:[],state:[]}));
  const result=validateDescription(d);
  assert.equal(result.valid,false);
  assert.ok(result.errors.some(e=>e.message.includes('16384')));
});

test('state must cover exactly the advertised props and bounded fields',()=>{
  const d=copy();
  d.props[0].state.push({id:'count',name:'Count',type:'number',min:0,max:3});
  const report={props:d.props.map(p=>({id:p.id,values:Object.fromEntries(p.state.map(f=>[f.id,f.type==='number'?1:false]))}))};
  assert.equal(validateState(d,report).valid,true);
  for (const change of [r=>r.props.pop(),r=>r.props[1]=r.props[0],r=>delete r.props[0].values.solved,r=>r.props[0].values.extra=true,r=>r.props[0].values.count=4,r=>r.props[0].values.solved='true',r=>r.extra=true]) {
    const bad=structuredClone(report);change(bad);assert.equal(validateState(d,bad).valid,false);
  }
});

test('CLI produces machine-readable success and nonzero failures',()=>{
  const cwd=new URL('../',import.meta.url);
  const good=spawnSync(process.execPath,['scripts/validate-description.mjs','tests/fixtures/description.json','--json'],{cwd,encoding:'utf8'});
  assert.equal(good.status,0,good.stderr);assert.equal(JSON.parse(good.stdout).valid,true);
  const bad=spawnSync(process.execPath,['scripts/validate-description.mjs','does-not-exist.json','--json'],{cwd,encoding:'utf8'});
  assert.equal(bad.status,1);assert.equal(JSON.parse(bad.stdout).valid,false);
});
