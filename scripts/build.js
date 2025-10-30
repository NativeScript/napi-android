#!/usr/bin/env node
// Simple build helper for NativeScript Android runtime.
// Supports interactive prompts or direct CLI flags.
// See README in the repo for Gradle usage.

const { spawn } = require('child_process');
const path = require('path');
const readline = require('readline');

const VALID_ENGINES = ['V8', 'QUICKJS', 'HERMES', 'JSC', 'SHERMES', 'PRIMJS'];
const HOST_OBJECTS_SUPPORTED = new Set(['V8', 'QUICKJS', 'PRIMJS']);

function parseArgs(argv) {
  const opts = {};
  argv.forEach(arg => {
    if (!arg.startsWith('--')) return;
    const eq = arg.indexOf('=');
    if (eq !== -1) {
      const k = arg.slice(2, eq);
      const v = arg.slice(eq + 1);
      opts[k] = v;
    } else {
      const k = arg.slice(2);
      // flag -> boolean true
      opts[k] = true;
    }
  });
  return opts;
}

async function prompt(question, rl, defaultVal) {
  return new Promise(resolve => {
    const q = defaultVal ? `${question} (${defaultVal}) ` : `${question} `;
    rl.question(q, ans => {
      if (!ans && typeof defaultVal !== 'undefined') return resolve(defaultVal);
      resolve(ans);
    });
  });
}

async function interactiveFill(opts) {
  const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
  try {
    // If skip-all-args is given, only ask for engine and host-objects (when supported).
    if (opts['skip-all-args']) {
      if (!opts.engine) {
        console.log('Select JS engine:');
        VALID_ENGINES.forEach((e, i) => console.log(`  ${i + 1}) ${e}`));
        const ans = await prompt('Choose number or name', rl, 'V8');
        const pick = /^\d+$/.test(ans) ? VALID_ENGINES[Number(ans) - 1] : ans;
        opts.engine = VALID_ENGINES.includes(pick) ? pick : 'V8';
      }

      // Only prompt for host objects if the chosen engine supports it
      if (HOST_OBJECTS_SUPPORTED.has(opts.engine)) {
        if (typeof opts['use-host-objects'] === 'undefined') {
          const ans = await prompt('Use host objects? [y/N]', rl, 'N');
          if (/^y(es)?$/i.test(ans)) opts['use-host-objects'] = true;
        }
      } else {
        // ensure the flag is not set for unsupported engines
        if (opts['use-host-objects']) {
          console.log(`Warning: host objects not supported for engine ${opts.engine}; ignoring --use-host-objects`);
          delete opts['use-host-objects'];
        }
      }

      return opts;
    }

    // original interactive flow (unchanged) when not skipping all args
    if (!opts.engine) {
      console.log('Select JS engine:');
      VALID_ENGINES.forEach((e, i) => console.log(`  ${i + 1}) ${e}`));
      const ans = await prompt('Choose number or name', rl, 'V8');
      const pick = /^\d+$/.test(ans) ? VALID_ENGINES[Number(ans) - 1] : ans;
      opts.engine = VALID_ENGINES.includes(pick) ? pick : 'V8';
    }

    const booleanPrompts = [
      { key: 'use-host-objects', prop: 'useHostObjects', desc: 'Use host objects (useHostObjects)' },
    ];

    for (const p of booleanPrompts) {
      // skip host-objects prompt if the selected engine does not support it
      if (p.key === 'use-host-objects' && !HOST_OBJECTS_SUPPORTED.has(opts.engine)) {
        if (opts['use-host-objects']) {
          console.log(`Warning: host objects not supported for engine ${opts.engine}; ignoring --use-host-objects`);
          delete opts['use-host-objects'];
        }
        continue;
      }

      if (typeof opts[p.key] === 'undefined') {
        const ans = await prompt(`${p.desc}? [y/N]`, rl, 'N');
        if (/^y(es)?$/i.test(ans)) opts[p.key] = true;
      }
    }

  } finally {
    rl.close();
  }

  return opts;
}

function buildGradleArgs(opts) {
  const props = [];
  if (opts.engine) props.push(`-Pengine=${opts.engine}`);
  if (opts['use-host-objects']) props.push('-PuseHostObjects');
  if (opts['as-napi-module']) props.push('-PasNapiModule');

  return props;
}

async function main() {
  const initial = parseArgs(process.argv.slice(2));
  const opts = await interactiveFill(initial);

  const gradleProps = buildGradleArgs(opts);
  const gradlew = process.platform === 'win32' ? 'gradlew' : './gradlew';
  const gradleCmd = [gradlew].concat(gradleProps, []);

  console.log('\nGradle command:');
  console.log(gradleCmd.join(' '), '\n');

  if (opts['dry-run']) {
    console.log('Dry run requested. Exiting without executing gradle.');
    return;
  }

  const proc = spawn(gradleCmd[0], gradleCmd.slice(1), { stdio: 'inherit', cwd: process.cwd(), shell: false });

  proc.on('exit', code => {
    if (code === 0) {
      console.log('\nBuild finished successfully.');
    } else {
      console.error(`\nBuild failed with exit code ${code}.`);
    }
    process.exit(code);
  });

  proc.on("message", (message) => {
    console.log(message);
  })

  proc.on('error', err => {
    console.error('Failed to start gradle:', err.message || err);
    process.exit(1);
  });
}

main().catch(err => {
  console.error('Error:', err && err.message ? err.message : err);
  process.exit(1);
});