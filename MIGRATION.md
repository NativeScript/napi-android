# V8 → NAPI runtime migration ledger

Tracks the port of substantive commits from the old V8-based runtime
(`nativescript/android-runtime`, branch `master`) into this NAPI-based runtime,
starting at `2bab8f5` (`fix(URL): allow undefined 2nd args`, #1826) through the
old runtime's HEAD (129 commits total).

Plan: `~/.claude/plans/we-want-to-migrate-modular-prism.md`

**Disposition:** `ported` · `already-present` · `partial` · `skipped` · `deferred`

| # | old hash | subject | disposition | new commit | notes |
|---|----------|---------|-------------|------------|-------|
| 1 | 2bab8f5 | fix(URL): allow undefined 2nd args (#1826) | ported | ef20f1d | `URL::New` (modules/url/URL.cpp): gate base-URL branch on `argv[1]` not being undefined/null via `napi_util::is_of_type`; ported expanded `testURLImpl.js` verbatim (JS is engine-agnostic). Verified: native recompile (V8-13, all ABIs) exit 0. |
| 2 | 94ddb15 | fix: `exit(0)` causes ANR due to destroyed mutex (#1820) | ported | this commit | Old fix was in `MetadataNode::BuildMetadata`; in napi runtime that error path moved to `MetadataBuilder.cpp:55` (identical direct-boot/locked-screen comment). Changed `exit(0)` → `_Exit(0)`. Verified: native recompile exit 0. |

## Verified duplication-check seeds (from planning; confirm at implementation time)

**MISSING → implement:** `f033061` queueMicrotask · Ada v3.x chain (`b31fc5f`/`3633aed`/`3513ce7`/`83f611b`/`45fb275`) — new bundles Ada 2.9.0, old 3.3.0.

**PARTIAL → port delta only:** `052cb21` ESM (deferred) · URLSearchParams spec follow-ups (`3e61cef`/`89893ae`/`288491f`).

**ALREADY-PRESENT → skip w/ evidence:** `e98367c` performance api (Performance.h) · `e293636` companion-object inner-type (MetadataNode::SetInnerTypes hasOwnProperty guard) · `1fd144f`/`df4e81b` multithreaded runtime selection (Runtime.java ConcurrentHashMap + dual-path getCurrentRuntimeId).

**Build deltas (new behind old):** compileSdk/targetSdk 34→35, buildTools 34→35, Kotlin 2.0.0→2.2.20, NDK default 27.1→27.3. AGP/Gradle 8.12.1, JDK 17, minSdk 21 already match. Apply only per-commit version/flag deltas; preserve multi-engine gradle/CMake machinery.
