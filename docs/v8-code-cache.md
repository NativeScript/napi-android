# V8 code cache

V8 has no ahead-of-time bytecode format like Hermes, but it can **serialize the
code it compiles at runtime** and consume it on the next launch to skip parsing
and compilation. This is wired into the V8 runtime and works by default — no
configuration required.

Unlike the [compile-time bytecode](./bytecode.md) feature (Hermes et al.), this is
a *runtime* cache: the first launch compiles as usual and writes the cache; every
later launch loads it.

## How it works

All script execution funnels through `js_execute_script`
([napi/v8/jsr.cpp](../test-app/runtime/src/main/cpp/napi/v8/jsr.cpp)), which now:

1. **Consumes** an existing cache via `js_run_cached_script`. If a `<file>.cache`
   sits next to the module and is current, V8 compiles from it (`kConsumeCodeCache`)
   and runs — no parse.
2. On a **miss**, compiles the source **once** (`CompileRunAndCache`), serializes
   a code cache from that same `UnboundScript`, then runs it. The cache is written
   next to the module for next time.

   > `napi_run_script_source` only returns the completion value, not the compiled
   > `UnboundScript` that `CreateCodeCache` needs — so the cold path compiles
   > directly (rather than delegating and then re-compiling) to avoid parsing the
   > source twice.

The cache lives at `<module>.cache` in the app's writable files dir (next to the
extracted JS). Only real `file://` modules are cached; synthetic sources such as
`<require_factory>` are skipped.

## Correctness

- **Staleness.** The `.cache` is stamped with its source file's mtime. If the
  source changes (app update, LiveSync), the mtime no longer matches and the
  cache is ignored, then rewritten. V8 additionally embeds a source hash and
  version tag in the cache and rejects it on any mismatch (e.g. a runtime/V8
  upgrade), recompiling from source — so a stale or incompatible cache can never
  run the wrong code. Rejected caches are refreshed automatically.
- **Exceptions.** `js_run_cached_script` distinguishes a *cache miss* (returns
  `napi_cannot_run_js`, caller falls back to source) from *the module actually
  running and throwing* (propagates the exception; the caller must not execute it
  again). This prevents double execution of module side effects.
- **Atomic writes.** The cache is written to `<file>.cache.tmp` and renamed into
  place (mtime stamped before the rename), so a crash or a worker loading the same
  module never sees a half-written cache.

## Cost

The win is on the **second and later** launches. The first launch (or any launch
after a change) compiles the source once — the same as before this feature — plus
a small serialize-and-write step to produce the cache. There is no double compile.
Real apps are bundled into a handful of files, so the extra work is negligible and
buys faster subsequent starts.

## Notes

- This is orthogonal to compile-time bytecode. V8 uses this runtime cache; Hermes
  (and, later, QuickJS/PrimJS) use precompiled bytecode instead.
- `js_run_bytecode_file` remains a no-op for V8 (there is no AOT bytecode format).
