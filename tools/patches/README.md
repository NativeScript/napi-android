# Engine source patches

The QuickJS / QuickJS-NG engine sources under
`test-app/runtime/src/main/cpp/napi/quickjs/{source,source_ng}` are git submodules
pinned to an upstream commit. Our hand-written NativeScript changes are kept here
as patches applied on top of that pristine checkout, rather than baked into a
vendored copy.

```
tools/patches/
  quickjs/     0001-nativescript-local-changes.patch   (bellard/quickjs)
  quickjs_ng/  0001-nativescript-local-changes.patch   (quickjs-ng/quickjs)
```

Both patches touch only `quickjs.c` / `quickjs.h`, which is where all our edits
live: the `USE_HOST_OBJECT` host-object hooks in `JS_GetPropertyInternal` (host
objects act as a non-masking prototype fallback) and the functions after the
`/* CUSTOM */` marker at the bottom of each file.

## Managing them

`scripts/vendor-engines-as-submodules.sh` owns this setup:

- **quickjs** patch is *auto-generated* by diffing the submodule against upstream
  (the vendored base tracks bellard master closely, so the diff is exactly our
  edits).
- **quickjs_ng** patch is *hand-migrated* and used as-is (`ENGINE_PATCH_MODE=manual`).
  The old vendored source was v0.15.1 (BC_VERSION 26) while the submodule is pinned
  to master `d950d55` (BC_VERSION 27), so a diff can't isolate our edits — the
  patch was ported onto master by hand and verified to compile.

On a fresh checkout / in CI:

```
git submodule update --init \
  test-app/runtime/src/main/cpp/napi/quickjs/source \
  test-app/runtime/src/main/cpp/napi/quickjs/source_ng
scripts/vendor-engines-as-submodules.sh --apply-only
```

## Compatibility

The submodule commit sets the engine's `BC_VERSION`. The compile-time bytecode
CLI (see `.github/workflows/bytecode-compilers.yml`) MUST be built from the same
commit, or `JS_ReadObject` will reject the produced bytecode at runtime. Keep the
workflow refs and the submodule pins in lock-step.
