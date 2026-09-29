// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const root=fileURLToPath(new URL('../',import.meta.url));
const output=process.argv[2];
if (!output) throw new Error('Usage: npm run package -- /absolute/path/to/new-directory');
const version=JSON.parse(readFileSync(join(root,'package.json'),'utf8')).version;
if (!readFileSync(join(root,'library.properties'),'utf8').includes(`version=${version}\n`)) throw new Error('Library and package versions differ');
const sources=['EscapeDirectorRoom.h','EscapeDirectorRoom.cpp','RoomRegistry.h','EscapeDirector.h','EscapeDirector.cpp','CompactAllocation.h','BoardAdapter.h','boards/GigaBoard.cpp','boards/GigaTls.h','boards/UnoR4Board.cpp','boards/PairingSlots.h','CommandWindow.h','NetworkRetry.h','StationAddress.h','StationClock.h','SerialJsonLine.h'];
const examples=['examples/room_basic/room_basic.ino','examples/button_code/button_code.ino'];
const libraryFiles=['library.properties','LICENSE','TRADEMARKS.md','THIRD_PARTY.md',...sources.map(f=>`src/${f}`),...examples];
const documents=['.gitignore','README.md','GETTING_STARTED.md','BOARD_PORTING.md','protocol.md','description.schema.json','AGENTS.md','CLAUDE.md','LICENSE','TRADEMARKS.md','THIRD_PARTY.md','CONTRIBUTING.md','docs/compatibility.md','docs/agent-integration.md','docs/room-api.md'];
// Every host test that scripts/test-cpp.mjs runs, so `npm run check` works in the download.
const testList=readFileSync(join(root,'scripts/test-cpp.mjs'),'utf8').match(/for \(const name of \[([^\]]+)\]/);
if (!testList) throw new Error('Host test list not found in scripts/test-cpp.mjs');
const hostTests=[...testList[1].matchAll(/'(\w+)'/g)].map(m=>m[1]);
const tooling=['package.json','package-lock.json','scripts/validate-description.mjs','scripts/test-cpp.mjs','scripts/package.mjs','tests/description.test.mjs','tests/fixtures/description.json',...hostTests.map(n=>`tests/${n}.cpp`),'tests/host/Arduino.h','tests/host/Client.h','tests/host/Udp.h'];
const sourceFiles=[...sources.map(f=>`src/${f}`),...examples,'library.properties'];
const bundleFiles=[...documents,...tooling,...sourceFiles];
const hash=file=>createHash('sha256').update(readFileSync(file)).digest('hex');
const revision=execFileSync('git',['rev-parse','HEAD'],{cwd:root,encoding:'utf8'}).trim();
if (execFileSync('git',['status','--porcelain'],{cwd:root,encoding:'utf8'}).trim()) throw new Error('Commit the reviewed source before packaging; working tree must be clean');
const destination=resolve(output);mkdirSync(destination);
const staging=mkdtempSync(join(tmpdir(),'device-sdk-package-'));
const bundleName=`EscapeDirectorSDK-${version}`;
const bundle=join(staging,bundleName);
const copy=(file,target)=>{mkdirSync(dirname(target),{recursive:true});copyFileSync(join(root,file),target);};
// The download must be self-contained: local includes and Markdown links resolve.
const checkBundle=(directory,files)=>{
  const problems=[];
  for (const file of files) {
    const text=readFileSync(join(directory,file),'utf8');
    if (/\.(h|cpp|ino)$/.test(file)) for (const [,include] of text.matchAll(/#include\s+"([^"]+)"/g)) {
      if (!existsSync(join(directory,dirname(file),include)) && !existsSync(join(directory,'src',include))) problems.push(`${file}: #include "${include}"`);
    }
    if (file.endsWith('.md')) for (const [,link] of text.matchAll(/\]\(([^)#\s]+)(#[^)]*)?\)/g)) {
      if (!/^[a-z]+:/.test(link) && !existsSync(join(directory,dirname(file),link))) problems.push(`${file}: link ${link}`);
    }
  }
  if (problems.length) throw new Error(`The package is not self-contained:\n${problems.join('\n')}`);
};
try {
  for (const file of libraryFiles) copy(file,join(staging,'EscapeDirector',file));
  checkBundle(join(staging,'EscapeDirector'),libraryFiles.filter(f=>/\.(h|cpp|ino)$/.test(f)));
  mkdirSync(bundle,{recursive:true});
  const libraryZip=`EscapeDirector-${version}.zip`;
  execFileSync('zip',['-q','-X',join(bundle,libraryZip),...libraryFiles.map(f=>`EscapeDirector/${f}`).sort()],{cwd:staging});
  for (const file of bundleFiles) copy(file,join(bundle,file));
  checkBundle(bundle,bundleFiles.filter(f=>/\.(h|cpp|ino|md)$/.test(f)));
  const files=[libraryZip,...bundleFiles];
  writeFileSync(join(bundle,'manifest.json'),JSON.stringify({version,sourceRevision:revision,license:'MIT',files:Object.fromEntries(files.sort().map(f=>[f,hash(join(bundle,f))]))},null,2)+'\n');
  files.push('manifest.json');
  const zipName=`${bundleName}.zip`;
  execFileSync('zip',['-q','-X',join(destination,zipName),...files.map(f=>`${bundleName}/${f}`).sort()],{cwd:staging});
  writeFileSync(join(destination,'SHA256SUMS'),`${hash(join(destination,zipName))}  ${zipName}\n`);
  console.log(join(destination,zipName));
} finally {rmSync(staging,{recursive:true,force:true});}
