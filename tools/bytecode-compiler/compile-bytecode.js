#!/usr/bin/env node
/**
 * Compile-time bytecode generator for NativeScript Android (engine-agnostic).
 *
 * Walks a directory of app JavaScript (normally the `app/` folder inside the
 * merged Android assets) and replaces every `.js` file with pre-compiled engine
 * bytecode for the selected JS engine. The files keep their `.js` names so the
 * runtime's module resolver is unaffected; the runtime detects the engine's
 * bytecode magic at load time and runs it directly instead of compiling source,
 * which dramatically improves time-to-interactive.
 *
 * This driver is generic. Everything engine-specific — which compiler binary to
 * run, how to build its command line, and what its bytecode looks like — lives
 * in a small adapter under ./lib (see lib/index.js for the contract). The only
 * Hermes-specific thing anywhere is running `hermesc`, encapsulated in
 * lib/hermes.js.
 *
 * IMPORTANT — module wrapping must match the runtime exactly.
 * The runtime wraps every `require`d module in a function before executing it
 * (see ModuleInternal::MODULE_PROLOGUE / MODULE_EPILOGUE in
 * runtime/src/main/cpp/runtime/module/ModuleInternal.cpp). Running a module's
 * bytecode must therefore yield that same wrapper function, so we wrap the
 * source with the identical prologue/epilogue before compiling. Files the
 * runtime runs unwrapped (raw scripts executed via Runtime::RunScript, e.g.
 * `internal/ts_helpers.js`) are compiled as-is. Keep the two constants below
 * byte-for-byte identical to ModuleInternal.cpp.
 *
 * Usage:
 *   node compile-bytecode.js --app <dir> --engine <ENGINE> [options]
 *
 * Options:
 *   --app <dir>        Directory to process recursively (required).
 *   --engine <name>    ns_engine value, e.g. HERMES, QUICKJS, QUICKJS_NG, PRIMJS,
 *                      V8-13, JSC (required). Engines without a bytecode compiler
 *                      are a no-op.
 *   --bin-dir <dir>    Root of the host-specific compiler binaries
 *                      (default: <this-dir>/bin). Resolved as
 *                      <bin-dir>/<engine>/<host>/<compiler>.
 *   --compiler <path>  Explicit compiler binary path (overrides --bin-dir lookup).
 *   --raw <paths>      Comma-separated app-relative paths compiled unwrapped
 *                      (default: "internal/ts_helpers.js").
 *   --source-maps      Emit source maps next to each file (<file>.map) when the
 *                      engine's compiler supports them.
 *   --optimize <flag>  Override the engine's default optimization flag.
 *   --strict           Fail if the compiler binary is missing (default: warn and
 *                      leave the app as source).
 *   --keep-going       Leave a file as source if its compile fails, instead of
 *                      aborting the build (default: abort).
 *   --verbose          Log every file processed.
 */

'use strict';

const fs = require('fs');
const path = require('path');
const os = require('os');
const { spawnSync } = require('child_process');

const { adapterForEngine, hostKey } = require('./lib');

// MUST stay identical to ModuleInternal::MODULE_PROLOGUE / MODULE_EPILOGUE.
const MODULE_PROLOGUE = '(function(module, exports, require, __filename, __dirname){ ';
const MODULE_EPILOGUE = '\n})';

function parseArgs(argv) {
  const opts = {
    app: null,
    engine: null,
    binDir: path.join(__dirname, 'bin'),
    compiler: null,
    raw: ['internal/ts_helpers.js'],
    sourceMaps: false,
    optimize: undefined,
    strict: false,
    keepGoing: false,
    verbose: false,
  };
  for (let i = 2; i < argv.length; i++) {
    const a = argv[i];
    switch (a) {
      case '--app': opts.app = argv[++i]; break;
      case '--engine': opts.engine = argv[++i]; break;
      case '--bin-dir': opts.binDir = argv[++i]; break;
      case '--compiler': opts.compiler = argv[++i]; break;
      case '--raw': opts.raw = argv[++i].split(',').map((s) => s.trim()).filter(Boolean); break;
      case '--source-maps': opts.sourceMaps = true; break;
      case '--optimize': opts.optimize = argv[++i]; break;
      case '--strict': opts.strict = true; break;
      case '--keep-going': opts.keepGoing = true; break;
      case '--verbose': opts.verbose = true; break;
      default: throw new Error(`Unknown argument: ${a}`);
    }
  }
  if (!opts.app) throw new Error('--app <dir> is required');
  if (!opts.engine) throw new Error('--engine <name> is required');
  return opts;
}

function listJsFiles(dir) {
  const out = [];
  const walk = (d) => {
    for (const entry of fs.readdirSync(d, { withFileTypes: true })) {
      const full = path.join(d, entry.name);
      if (entry.isDirectory()) walk(full);
      else if (entry.isFile() && entry.name.endsWith('.js')) out.push(full);
    }
  };
  walk(dir);
  return out;
}

function readHead(file, n) {
  const fd = fs.openSync(file, 'r');
  try {
    const head = Buffer.alloc(n);
    const read = fs.readSync(fd, head, 0, n, 0);
    return head.subarray(0, read);
  } finally {
    fs.closeSync(fd);
  }
}

/** Normalize to a forward-slashed app-relative path for raw-file matching. */
function relKey(appDir, file) {
  return path.relative(appDir, file).split(path.sep).join('/');
}

function resolveCompiler(opts, adapter, host) {
  if (opts.compiler) return opts.compiler;
  return path.join(opts.binDir, adapter.key, host, adapter.binName(host));
}

function isPlaceholder(file) {
  // Dummy host slots are laid out as small text (README/.gitkeep), never a real
  // executable. Treat anything tiny as "not a real compiler".
  try {
    return fs.statSync(file).size < 1024;
  } catch (_) {
    return true;
  }
}

function compileFile(opts, adapter, compiler, file, isRaw, appDir) {
  const source = fs.readFileSync(file, 'utf8');
  const wrapped = isRaw ? source : MODULE_PROLOGUE + source + MODULE_EPILOGUE;

  // Embed the app-relative path as the module's source name so runtime stack
  // traces point at the app's file (e.g. "shared/index.js"), not a throwaway
  // temp path. Every engine derives the embedded name from the *input path we
  // pass*: hermesc bakes in its argv path verbatim (it never absolutizes a
  // cwd-relative path), and the qjs/primjs shims hand their input path straight
  // to JS_Eval/LEPUS_Eval as the filename. So we mirror the wrapped source under
  // a temp root at its app-relative path and run the compiler from that root with
  // the relative path as input — the embedded name becomes exactly `rel`.
  //
  // The device-absolute "file:///data/data/<app>/files/app/..." form can't be
  // used here: hermesc must be given a real, openable host file (no URL scheme /
  // device path), and that prefix is device-specific anyway. The app-relative
  // path is the identity all engines can embed consistently.
  const rel = relKey(appDir, file);
  const relParts = rel.split('/');
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'nsbc-'));
  const tmpSrc = path.join(tmpDir, ...relParts);
  const tmpOut = path.join(tmpDir, path.basename(file) + '.bc');
  const wantMap = opts.sourceMaps && adapter.supportsSourceMaps;
  try {
    fs.mkdirSync(path.dirname(tmpSrc), { recursive: true });
    fs.writeFileSync(tmpSrc, wrapped);
    const optimize = opts.optimize !== undefined ? opts.optimize : adapter.defaultOptimize;
    const args = adapter.buildArgs({
      // Pass the app-relative path (forward-slashed); with cwd = tmpDir the
      // compiler opens tmpDir/<rel> and embeds "<rel>" as the source name.
      input: rel,
      output: tmpOut,
      sourceMap: wantMap ? adapter.sourceMapOutput(tmpOut) : null,
      optimize,
    });
    const res = spawnSync(compiler, args, { cwd: tmpDir, encoding: 'utf8' });
    if (res.status !== 0) {
      const detail = (res.stderr || res.stdout || (res.error && res.error.message) || '').toString().trim();
      throw new Error(`compiler failed for ${file} (exit ${res.status}):\n${detail}`);
    }
    const bytecode = fs.readFileSync(tmpOut);
    if (adapter.magic && !adapter.isBytecode(bytecode.subarray(0, adapter.magic.length))) {
      throw new Error(`compiler produced non-bytecode output for ${file}`);
    }
    // Overwrite in place: the file keeps its .js name; the runtime detects the magic.
    fs.writeFileSync(file, bytecode);
    if (wantMap) {
      const mapSrc = adapter.sourceMapOutput(tmpOut);
      if (fs.existsSync(mapSrc)) fs.copyFileSync(mapSrc, file + '.map');
    }
    return bytecode.length;
  } finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }
}

function main() {
  const opts = parseArgs(process.argv);
  const appDir = path.resolve(opts.app);
  const host = hostKey();

  const adapter = adapterForEngine(opts.engine);
  if (!adapter) {
    console.log(`[bytecode] engine ${opts.engine} has no bytecode compiler — leaving app as source.`);
    return;
  }
  if (!adapter.ready) {
    console.log(`[bytecode] ${adapter.key} bytecode is not enabled yet — leaving app as source.`);
    return;
  }
  if (!fs.existsSync(appDir)) {
    console.log(`[bytecode] app dir not found, nothing to do: ${appDir}`);
    return;
  }

  const compiler = resolveCompiler(opts, adapter, host);
  if (!fs.existsSync(compiler) || (!opts.compiler && isPlaceholder(compiler))) {
    const msg = `[bytecode] no ${adapter.key} compiler for host ${host} at ${compiler}`;
    if (opts.strict) throw new Error(msg + ' (--strict)');
    console.warn(`${msg} — leaving app as source.`);
    return;
  }

  // Ensure the compiler is executable. Binaries shipped via CI artifact zips,
  // npm packages, or restored without the exec bit lose it, which surfaces as a
  // spawn EACCES. (No-op on Windows.)
  if (process.platform !== 'win32') {
    try {
      fs.chmodSync(compiler, 0o755);
    } catch (err) {
      console.warn(`[bytecode] could not chmod +x ${compiler}: ${err.message}`);
    }
  }

  const rawSet = new Set(opts.raw);
  const magicLen = adapter.magic ? adapter.magic.length : 0;
  const files = listJsFiles(appDir);
  let compiled = 0;
  let skipped = 0;
  let rawCount = 0;
  const start = Date.now();

  for (const file of files) {
    if (magicLen && adapter.isBytecode(readHead(file, magicLen))) {
      // Idempotent: safe to re-run on already-processed (e.g. cached) assets.
      skipped++;
      continue;
    }
    const isRaw = rawSet.has(relKey(appDir, file));
    try {
      compileFile(opts, adapter, compiler, file, isRaw, appDir);
      compiled++;
      if (isRaw) rawCount++;
      if (opts.verbose) {
        console.log(`[bytecode] compiled${isRaw ? ' (raw)' : ''}: ${relKey(appDir, file)}`);
      }
    } catch (err) {
      if (opts.keepGoing) {
        console.warn(`[bytecode] WARNING: leaving as source: ${err.message}`);
      } else {
        throw err;
      }
    }
  }

  const ms = Date.now() - start;
  console.log(
    `[bytecode] ${adapter.key}/${host}: compiled ${compiled} file(s) ` +
    `(${rawCount} raw), skipped ${skipped} already-compiled, in ${ms}ms.`
  );
}

try {
  main();
} catch (err) {
  console.error(`[bytecode] ERROR: ${err.message}`);
  process.exit(1);
}
