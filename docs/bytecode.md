# Compile-time bytecode

For **release** builds, NativeScript Android can ship pre-compiled engine
bytecode instead of plain JavaScript. Parsing + compiling JS is a large part of
cold-start cost; shipping bytecode moves that work to build time and gives a much
faster time-to-interactive (TTI).

The mechanism is **engine-generic**. The only engine-specific piece is running
that engine's compiler binary, which is encapsulated in a small adapter under
[`tools/bytecode-compiler/lib`](../tools/bytecode-compiler/lib).

| Engine  | Bytecode | Compiler CLI | Status |
| ------- | -------- | ------------ | ------ |
| Hermes  | ✅ HBC   | `hermesc` (upstream) | wired end-to-end |
| QuickJS | ✅       | `nsbc-quickjs` shim (`native/qjs-compile.c`) | CLI built by CI; runtime read-support pending |
| QuickJS-NG | ✅    | `nsbc-quickjs-ng` shim | CLI built by CI; runtime read-support pending |
| PrimJS  | ✅       | `nsbc-primjs` shim (`native/primjs-compile.c`) | CLI built by CI; runtime read-support pending |
| V8      | ⚠️       | runtime code-cache only (no AOT format) | see [v8-code-cache.md](./v8-code-cache.md) |
| JSC     | ❌ never | —          | n/a |

Only Hermes is enabled today. The compiler CLIs for the QuickJS family are built
by the [bytecode-compilers workflow](../.github/workflows/bytecode-compilers.yml),
but those engines stay gated off (`ready: false`) until their runtime execution
path (`js_run_bytecode_file`) exists — see "Adding an engine" below.

### Bytecode container (non-Hermes engines)

`hermesc` emits a self-identifying HBC blob. QuickJS's `qjsc` emits a C array, not
a loadable blob, so the QuickJS-family shims (`native/`) wrap the engine's
`JS_WriteObject` output in a small container the runtime detects:

```
[8-byte magic][4-byte format version, little-endian][JS_WriteObject payload]
```

Magic is per engine (`NSBCQJS\0`, `NSBCNGS\0`, `NSBCPJS\0`) and must match the
adapter's `magic` and the runtime detection. The runtime read side strips the
12-byte header and hands the payload to `JS_ReadObject` + `JS_EvalFunction`.

## How it works

1. A normal app compiles its JS bundle and copies it into `assets/app` before the
   Android build starts.
2. During a **release** build, after Gradle merges the assets, the
   `compileBytecode` task runs the generic driver over the merged `app/` folder.
   The driver replaces every `.js` file with bytecode for the active engine.
   **The files keep their `.js` names**, so module resolution is unaffected.
3. At load time the runtime reads the file header. If it sees the engine's
   bytecode magic (for Hermes, `0x1F1903C103BC1FC6`) it runs the bytecode
   directly; otherwise it falls back to compiling the source. Detection is
   per-file and authoritative — there is no separate flag that can get out of
   sync.

The C++ loader calls a single engine hook for both the `require` path
([ModuleInternal.cpp](../test-app/runtime/src/main/cpp/runtime/module/ModuleInternal.cpp))
and the raw-script path
([Runtime::RunScript](../test-app/runtime/src/main/cpp/runtime/Runtime.cpp)):

```c
// napi/common/jsr_common.h
napi_status js_run_bytecode_file(napi_env env, const char *file,
                                 const char *source_url, napi_value *result);
```

### Module wrapping

The runtime wraps every `require`d module in a function before executing it:

```js
(function(module, exports, require, __filename, __dirname){ /* module source */
})
```

Running a module's bytecode must yield that same wrapper function, so the build
step wraps the source with the **identical** prologue/epilogue before compiling
(the completion value of the compiled program is the wrapper function — verified
by disassembly: the Hermes global function ends with `CreateClosure; Ret`). This
wrapping is engine-independent, so it lives in the generic driver.

Files the runtime runs **unwrapped** as raw scripts (via `Runtime::RunScript`,
e.g. `internal/ts_helpers.js`) are compiled as-is. The build's `--raw` list keeps
these in sync (default: `internal/ts_helpers.js`).

The prologue/epilogue live in two places that must stay byte-for-byte identical:

- `runtime/src/main/cpp/runtime/module/ModuleInternal.cpp`
  (`MODULE_PROLOGUE` / `MODULE_EPILOGUE`)
- `tools/bytecode-compiler/compile-bytecode.js`
  (`MODULE_PROLOGUE` / `MODULE_EPILOGUE`)

## The compiler tooling

The compiler lives in [`tools/bytecode-compiler`](../tools/bytecode-compiler) and
is copied into the framework template under `build-tools/bytecode-compiler` when
the runtime is packaged (see `copyFilesToProjectTemeplate` in the root
`build.gradle`). Only the active engine's host binaries are shipped, so a V8
template doesn't carry `hermesc`.

Compiler binaries are host-specific, laid out as
`bin/<engine>/<host>/<compiler>` where `<host>` is `<platform>-<arch>`
(`darwin-arm64`, `darwin-x64`, `linux-x64`, `linux-arm64`, `win32-x64`). Host
slots without a real binary contain a placeholder; the driver detects the current
host automatically and, if there's no compiler for it, leaves the app as source.

## Enabling / disabling

Bytecode is **on by default** for release builds when the active engine has a
ready compiler for the build host. To disable it (ship plain JS):

```
./gradlew assembleRelease -Prelease -PnsBytecodeDisabled
```

Gradle properties (all optional):

| Property                       | Effect                                            |
| ------------------------------ | ------------------------------------------------- |
| `-PnsBytecodeDisabled`         | Disable bytecode for this release build           |
| `-PnsBytecodeSourceMaps`       | Emit `<file>.map` next to each file when supported |
| `-PbytecodeToolsDir=<dir>`     | Override the bytecode-compiler tools folder       |
| `-PbytecodeCompilerBinary=<p>` | Force a specific compiler binary                  |
| `-PnodePath=<path>`            | Override the `node` executable                    |

The compile step never touches the app source — it only rewrites the merged-assets
build intermediate that gets packaged into the APK, and it is idempotent (files
already in bytecode form are skipped), so incremental builds are safe.

## Source maps

With `-PnsBytecodeSourceMaps`, engines whose compiler supports it (Hermes) emit a
source map next to each file (`<file>.map`). Because the module wrapper is a
single-line prologue with no newlines, line numbers are preserved (only a
column offset on line 1).

## Version compatibility

A compiler and the runtime's engine library must share the same bytecode version
(for Hermes, `BYTECODE_VERSION`, currently 99). They should be built from the same
engine checkout. On a mismatch the runtime fails loudly at load rather than
silently misbehaving.

## Adding an engine

1. Add `tools/bytecode-compiler/lib/<engine>.js` implementing the adapter contract
   (documented in `lib/index.js`) and register it in `lib/index.js`.
2. Drop the host compiler binaries under `tools/bytecode-compiler/bin/<engine>/<host>/`.
3. Implement runtime execution in `napi/<engine>/jsr.cpp` — `js_run_bytecode_file`
   currently returns `napi_cannot_run_js` (source fallback) for every engine
   except Hermes. It must detect that engine's bytecode magic and run it, setting
   `*result` to the completion value (the module wrapper function for modules).
4. Flip the adapter's `ready` flag to `true` **only** once both the compiler and
   the runtime support exist — otherwise the build would ship bytecode the runtime
   can't execute.
