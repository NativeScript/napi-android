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
| 2 | 94ddb15 | fix: `exit(0)` causes ANR due to destroyed mutex (#1820) | ported | b46586b | Old fix was in `MetadataNode::BuildMetadata`; in napi runtime that error path moved to `MetadataBuilder.cpp:55` (identical direct-boot/locked-screen comment). Changed `exit(0)` → `_Exit(0)`. Verified: native recompile exit 0. |
| — | f290ed2 | perf: optimizations around generating JS classes from Metadata (#1824) | already-present | — | Large metadata-subsystem perf refactor (13 files). Confirmed by user: already implemented in the napi runtime's rewritten metadata code. Skipped. |
| 3 | a983931 | fix: gradle error when compileSdk or targetSdk is provided (#1825) | ported | 6f01665 | `test-app/app/build.gradle`: add `as int` cast to provided `compileSdk`/`targetSdk` in `computeCompileSdkVersion`/`computeTargetSdkVersion` (props arrive as String from `-P`). Verified: `:app:help -PcompileSdk=35 -PtargetSdk=35` config eval exit 0. |
| — | e293636 | fix: inner type not set when companion object defined as function (#1831) | already-present | — | New `MetadataNode::SetInnerTypes` (metadata/MetadataNode.cpp:1726-1733) already has the `napi_has_own_property` + `if(!hasOwnProperty)` guard before defining the inner-type accessor — napi port of the same fix. Skipped. |
| 5 | b31fc5f + 3633aed + 83f611b + 3513ce7 + 45fb275 | Ada v3 + URLPattern (#1830), Ada 3.1.1/3.1.3/3.2.7/3.3.0 (#1832/#1835/#1884) | ported | this commit | **Consolidated the whole Ada v3 chain** (Ada is a vendored single-file lib, so intermediate bumps collapse to the final): replaced `modules/url/ada/ada.{h,cpp}` 2.9.0→3.3.0 (verified no API breaks in URL/URLSearchParams). **Ported URLPattern** V8→napi: new `modules/url/URLPattern.{cpp,h}` with a `napi_regex_provider` (RegExp via global ctor + `.exec`, move-only `NapiRegex` ref wrapper, thread-local env for `create_instance`); wired `URLPattern::Init` into `NSRuntimeModules`. Registered spec-correct `hasRegExpGroups` (old C++ had typo `hasRegexpGroups`). Ported `testURLPattern.js` + registered in mainpage.js. Skipped the commit's gradle/CMake/Runtime-V8 bits (superseded / engine-specific). Verified: native build all ABIs exit 0. |
| 6 | bec401c | feat: NDK 27 and Support for Java 21 (#1819) | partial | this commit | **Ported (Java-21 source compat):** `NanoWSD.java` byte casts (`header |= (byte)…`); `NativeScriptAbstractMap` `SimpleEntry`/`SimpleImmutableEntry` gain `<K/V extends Serializable>` bounds; generator `build.gradle` ×2 `'17'`→`JavaVersion.VERSION_17`; add `compileOptions VERSION_17` to runtime module. **Already-present:** `-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON` (runtime/build.gradle:173), armeabi/x86 android_support block already removed. **Skipped (superseded/N-A to fork):** CI workflows, `gradlew`/wrapper (fork on gradle 8.14.3 > 8.9), root `package.json` version_info, dts-generator submodule bump. Verified: `:runtime:compileDebugJavaWithJavac` under **JDK 21** exit 0. |
| — | c9a2a86 | feat: update Gradle 8.14.3 and Android build tools 8.12.1 (#1834) | already-present | — | Fork already on gradle wrapper 8.14.3 and `NS_DEFAULT_ANDROID_BUILD_TOOLS_VERSION=8.12.1`. Skipped. |
| — | e98367c | feat: performance api (#1838) | already-present | — | `runtime/performance/Performance.h` registers `global.performance`/`performance.now()` (installed at Runtime.cpp:313). Skipped. |
| 7 | 87f7f9c | feat: update libzip to 1.11.4 (#1845) | ported | this commit | Vendored binary dep. Replaced `cpp/zip/include/{zip.h,zipconf.h}` and the 4 prebuilt `libs/common/<abi>/libzip.a` with upstream 1.11.4 (fork's zipconf was mislabeled "0.11"). Sole consumer `AssetExtractor.cpp` uses only stable API (zip_open/fread/stat_index/…). Verified: native compile+link all ABIs exit 0. **Caveat:** binary swap verified to link only — on-device asset extraction should be smoke-tested. |
| 4 | 3423e6f | feat: support 16 KB page sizes, gradle 8.5 (#1818) | partial | ca93e66 | **Ported:** 16 KB `-Wl,-z,max-page-size=16384` link option for arm64-v8a/x86_64 in `runtime/CMakeLists.txt` (verified present in ninja LINK_FLAGS); bumped `NS_DEFAULT_COMPILE_SDK_VERSION`/`NS_DEFAULT_BUILD_TOOLS_VERSION` 34→35 (both installed). **Skipped:** gradle-wrapper 8.4→8.7 and AGP 8.3.2→8.5.0 (new already newer: gradle 8.14.3 / AGP 8.12.1); STL `c++_shared`→`c++_static` (new deliberately uses `c++_shared` for multi-engine libc++ — would break engine `.so` setup). Verified: native reconfigure+relink exit 0. |

## Verified duplication-check seeds (from planning; confirm at implementation time)

**MISSING → implement:** `f033061` queueMicrotask · Ada v3.x chain (`b31fc5f`/`3633aed`/`3513ce7`/`83f611b`/`45fb275`) — new bundles Ada 2.9.0, old 3.3.0.

**PARTIAL → port delta only:** `052cb21` ESM (deferred) · URLSearchParams spec follow-ups (`3e61cef`/`89893ae`/`288491f`).

**ALREADY-PRESENT → skip w/ evidence:** `e98367c` performance api (Performance.h) · `e293636` companion-object inner-type (MetadataNode::SetInnerTypes hasOwnProperty guard) · `1fd144f`/`df4e81b` multithreaded runtime selection (Runtime.java ConcurrentHashMap + dual-path getCurrentRuntimeId).

**Build deltas (new behind old):** compileSdk/targetSdk 34→35, buildTools 34→35, Kotlin 2.0.0→2.2.20, NDK default 27.1→27.3. AGP/Gradle 8.12.1, JDK 17, minSdk 21 already match. Apply only per-commit version/flag deltas; preserve multi-engine gradle/CMake machinery.
