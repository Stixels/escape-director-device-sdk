// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { copyFileSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const root=fileURLToPath(new URL('../',import.meta.url));
const output=process.argv[2];
if (!output) throw new Error('Usage: npm run package -- /absolute/path/to/new-directory');
const version=JSON.parse(readFileSync(join(root,'package.json'),'utf8')).version;
if (!readFileSync(join(root,'library.properties'),'utf8').includes(`version=${version}\n`)) throw new Error('Library and package versions differ');
const sources=['EscapeDirector.h','EscapeDirector.cpp','CompactAllocation.h','BoardAdapter.h','boards/GigaBoard.cpp','boards/GigaTls.h','boards/UnoR4Board.cpp','boards/PairingSlots.h','EscapeDirectorDevice.h','Authority.h','CommandWindow.h','NetworkRetry.h','StationAddress.h','StationClock.h','CompactJson.h','SerialJsonLine.h'];
const examples=['two_props.ino','Description.h','TwoProps.h','description.json'];
const libraryFiles=['library.properties','LICENSE','TRADEMARKS.md','THIRD_PARTY.md',...sources.map(f=>`src/${f}`),...examples.map(f=>`examples/two_props/${f}`)];
const documents=['.gitignore','README.md','GETTING_STARTED.md','BOARD_PORTING.md','protocol.md','description.schema.json','AGENTS.md','CLAUDE.md','LICENSE','TRADEMARKS.md','THIRD_PARTY.md','CONTRIBUTING.md','docs/compatibility.md','docs/agent-integration.md'];
const tooling=['package.json','package-lock.json','scripts/validate-description.mjs','scripts/test-cpp.mjs','scripts/package.mjs','tests/description.test.mjs',...['deviceSdkRuntime','stationClock','compactJson','compactAllocation','pairingSlots'].map(n=>`tests/${n}.cpp`)];
const sourceFiles=[...sources.map(f=>`src/${f}`),...examples.map(f=>`examples/two_props/${f}`),'library.properties'];
const bundleFiles=[...documents,...tooling,...sourceFiles];
const hash=file=>createHash('sha256').update(readFileSync(file)).digest('hex');
const revision=execFileSync('git',['rev-parse','HEAD'],{cwd:root,encoding:'utf8'}).trim();
if (execFileSync('git',['status','--porcelain'],{cwd:root,encoding:'utf8'}).trim()) throw new Error('Commit the reviewed source before packaging; working tree must be clean');
const destination=resolve(output);mkdirSync(destination);
const staging=mkdtempSync(join(tmpdir(),'device-sdk-package-'));
const bundleName=`EscapeDirectorSDK-${version}`;
const bundle=join(staging,bundleName);
const copy=(file,target)=>{mkdirSync(dirname(target),{recursive:true});copyFileSync(join(root,file),target);};
try {
  for (const file of libraryFiles) copy(file,join(staging,'EscapeDirector',file));
  mkdirSync(bundle,{recursive:true});
  const libraryZip=`EscapeDirector-${version}.zip`;
  execFileSync('zip',['-q','-X',join(bundle,libraryZip),...libraryFiles.map(f=>`EscapeDirector/${f}`).sort()],{cwd:staging});
  for (const file of bundleFiles) copy(file,join(bundle,file));
  const files=[libraryZip,...bundleFiles];
  writeFileSync(join(bundle,'manifest.json'),JSON.stringify({version,sourceRevision:revision,license:'MIT',files:Object.fromEntries(files.sort().map(f=>[f,hash(join(bundle,f))]))},null,2)+'\n');
  files.push('manifest.json');
  const zipName=`${bundleName}.zip`;
  execFileSync('zip',['-q','-X',join(destination,zipName),...files.map(f=>`${bundleName}/${f}`).sort()],{cwd:staging});
  writeFileSync(join(destination,'SHA256SUMS'),`${hash(join(destination,zipName))}  ${zipName}\n`);
  console.log(join(destination,zipName));
} finally {rmSync(staging,{recursive:true,force:true});}
