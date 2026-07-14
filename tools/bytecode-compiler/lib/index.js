'use strict';

/**
 * Engine registry + host resolution for the generic bytecode compiler.
 *
 * The driver (compile-bytecode.js) is engine-agnostic: it walks the app JS,
 * wraps modules, handles idempotency and source maps, and shells out to a
 * compiler binary. Everything engine-specific — which binary to run, how to
 * build its command line, what its bytecode looks like — lives in an adapter
 * under this folder and is registered below.
 *
 * Adapter contract — see hermes.js for a fully-implemented example:
 *   key                 : string            adapter id (also the bin/<key> dir)
 *   engineKeys          : string[]          ns_engine values that select it (case-insensitive)
 *   ready               : boolean           false => driver skips (leaves source) even if a binary exists.
 *                                           Gate this on BOTH a real compiler AND runtime support for
 *                                           the produced bytecode, so we never ship a bytecode file the
 *                                           runtime can't execute.
 *   binName(hostKey)    : string            compiler filename for the host (e.g. 'hermesc'/'hermesc.exe')
 *   magic               : Buffer            leading bytes every bytecode file for this engine starts with
 *   supportsSourceMaps  : boolean
 *   defaultOptimize     : string|null       optimization flag passed as {optimize}
 *   isBytecode(head)    : boolean           true if `head` (>= magic.length bytes) is this engine's bytecode
 *   buildArgs(ctx)      : string[]          argv (after the binary) to compile ctx.input -> ctx.output.
 *                                           ctx = { input, output, sourceMap|null, optimize }
 *   sourceMapOutput(out): string            path the compiler writes the map to (moved to <file>.map)
 */

const HOST_KEYS = ['darwin-arm64', 'darwin-x64', 'linux-x64', 'linux-arm64', 'win32-x64'];

function hostKey() {
  return `${process.platform}-${process.arch}`;
}

const adapters = [
  require('./hermes'),
  require('./quickjs'),
  require('./quickjs-ng'),
  require('./primjs'),
];

/** Resolve an adapter from an ns_engine value (e.g. "HERMES", "QUICKJS_NG"). */
function adapterForEngine(engine) {
  if (!engine) return null;
  const e = String(engine).toUpperCase();
  return adapters.find((a) => a.engineKeys.some((k) => k.toUpperCase() === e)) || null;
}

module.exports = { adapters, adapterForEngine, hostKey, HOST_KEYS };
