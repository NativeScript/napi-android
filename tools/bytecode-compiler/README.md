# Compile-time bytecode compiler

Engine-generic tool that turns the app's plain-JS bundle into pre-compiled engine
bytecode at build time, so the runtime skips parsing/compiling on cold start.
Used by the `compileBytecode` Gradle task for **release** builds. See
[`docs/bytecode.md`](../../docs/bytecode.md) for the end-to-end design.

## Layout

```
bytecode-compiler/
  compile-bytecode.js      generic driver (walk, wrap, idempotency, source maps, host resolution)
  lib/
    index.js               engine registry + host resolution
    hermes.js              Hermes adapter (wired end-to-end today)
    quickjs.js             wired; gated off (ready:false) until runtime read-support lands
    quickjs-ng.js
    primjs.js
  native/
    qjs-compile.c          blob-emitting shim for QuickJS + QuickJS-NG (JS_* API)
    primjs-compile.c       blob-emitting shim for PrimJS (LEPUS_* API)
  bin/
    <engine>/<host>/<compiler>   host-specific compiler binaries (produced by CI)
```

## Building the compiler binaries

The host binaries are built by [`.github/workflows/bytecode-compilers.yml`](../../.github/workflows/bytecode-compilers.yml)
(manual `workflow_dispatch`). It clones each engine's **upstream** repo
(facebook/hermes, bellard/quickjs, quickjs-ng/quickjs, lynx-family/primjs), builds
the CLI for every host, and uploads one artifact per `(engine, host)` named
`bytecode-compiler-<engine>-<host>`. Download and drop them into
`bin/<engine>/<host>/`.

`hermesc` already emits a loadable HBC blob. QuickJS's `qjsc` does **not** (it
emits a C array), so for the QuickJS family we build a tiny shim from `native/`
that links the engine and emits a **NativeScript bytecode container**:

```
[8-byte magic][4-byte format version, little-endian][engine JS_WriteObject payload]
```

The magic (`NSBCQJS\0`, `NSBCNGS\0`, `NSBCPJS\0`) must match the adapter's `magic`
and the runtime's detection. Hermes uses its native HBC magic instead (no container).

> Compatibility: the CLI must be built from the engine ref that matches what the
> runtime bundles (prebuilt `.so` for Hermes/PrimJS, vendored source for
> QuickJS/QuickJS-NG). Pin the workflow inputs to exact commits once validated.

Only the compiler binary and its command line are engine-specific; that lives in
each adapter. Everything else (module wrapping, the raw-file list, idempotency,
source maps, picking the host binary) is generic.

## Host binaries

Compilers are native executables, so there is one per build host. `<host>` is
`<process.platform>-<process.arch>`:

| Host             | Directory        |
| ---------------- | ---------------- |
| macOS (Apple Si) | `darwin-arm64`   |
| macOS (Intel)    | `darwin-x64`     |
| Linux (x64)      | `linux-x64`      |
| Linux (arm64)    | `linux-arm64`    |
| Windows (x64)    | `win32-x64`      |

Slots we don't have a binary for yet contain a placeholder `README.md`. The driver
treats any slot without a real (>1 KB) executable as "no compiler for this host"
and leaves the app as plain JS source (which the runtime runs fine).

## Usage

```
node compile-bytecode.js --app <dir> --engine <HERMES|QUICKJS|QUICKJS_NG|PRIMJS|...> \
    [--source-maps] [--compiler <path>] [--bin-dir <dir>] [--raw a,b] [--strict] [--verbose]
```

Engines without an adapter (V8*, JSC) and not-yet-ready adapters are a no-op.

## Adding an engine

1. Add `lib/<engine>.js` implementing the adapter contract (see `lib/index.js`)
   and register it in `lib/index.js`.
2. Provide a compiler: either the engine ships a blob-emitting CLI (like
   `hermesc`), or add a shim under `native/` and a build job in the workflow. Get
   the host binaries into `bin/<engine>/<host>/`.
3. Implement the runtime execution side in `napi/<engine>/jsr.cpp`
   (`js_run_bytecode_file`) so it detects this engine's `magic` and runs the
   payload.
4. Flip the adapter's `ready` flag to `true` **only** once both the compiler and
   the runtime support exist — otherwise the build would ship bytecode the
   runtime can't run.
