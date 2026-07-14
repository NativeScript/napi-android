#!/usr/bin/env node
//
// One-shot developer/CI setup for the android-runtime repo:
//   1. Fetch the engine source submodules (QuickJS / QuickJS-NG) and reset them
//      to their pinned commit.
//   2. Apply the NativeScript local patches onto the pristine submodules.
//   3. Install the jsparser build-tool dependencies (the previous `npm run setup`).
//
// Idempotent: re-running resets the submodules to the pinned commit (discarding
// an already-applied patch) and re-applies, so it is safe to run repeatedly.
//
// The submodule + patch config mirrors scripts/vendor-engines-as-submodules.sh.

'use strict';

const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const NPM = process.platform === 'win32' ? 'npm.cmd' : 'npm';

const NAPI_QJS = 'test-app/runtime/src/main/cpp/napi/quickjs';
const ENGINES = [
  { sub: `${NAPI_QJS}/source`,    patch: 'tools/patches/quickjs/0001-nativescript-local-changes.patch' },
  { sub: `${NAPI_QJS}/source_ng`, patch: 'tools/patches/quickjs_ng/0001-nativescript-local-changes.patch' },
];

function run(cmd, args, opts = {}) {
  console.log(`==> ${cmd} ${args.join(' ')}`);
  execFileSync(cmd, args, { stdio: 'inherit', cwd: ROOT, ...opts });
}

function insideGitWorkTree() {
  try {
    return execFileSync('git', ['rev-parse', '--is-inside-work-tree'], { cwd: ROOT })
      .toString().trim() === 'true';
  } catch (_) {
    return false;
  }
}

function setupEngines() {
  if (!insideGitWorkTree()) {
    console.log('==> Not a git work tree; skipping submodule + patch setup.');
    return;
  }

  // 1. Fetch + reset the submodules to their pinned commit. --force discards any
  //    previously-applied patch so step 2 always starts from a clean checkout.
  run('git', ['submodule', 'update', '--init', '--force', ...ENGINES.map((e) => e.sub)]);

  // 2. Apply the local patches onto the pristine submodules.
  for (const { sub, patch } of ENGINES) {
    const patchAbs = path.join(ROOT, patch);
    if (!fs.existsSync(patchAbs)) {
      console.warn(`   ! patch missing, skipping: ${patch}`);
      continue;
    }
    run('git', ['-C', sub, 'apply', '--whitespace=nowarn', patchAbs]);
  }
}

function setupJsParser() {
  const dir = path.join(ROOT, 'test-app/build-tools/jsparser');
  const hasLock = fs.existsSync(path.join(dir, 'package-lock.json'));
  run(NPM, [hasLock ? 'ci' : 'install'], { cwd: dir });
}

setupEngines();
setupJsParser();
console.log('==> Setup complete');
