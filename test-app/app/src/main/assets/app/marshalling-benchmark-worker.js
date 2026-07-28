// Runs the runtime marshalling benchmark suite off the main (UI) thread so the
// app stays responsive and Android does not raise an ANR while benchmarking.

// CRITICAL: a Worker gets its own runtime context, which defaults to verbose
// native logging ON (main.js disables it only on the main thread). Left on, the
// runtime logs "CallJavaMethod ..." for EVERY marshalled call — flooding logcat
// and adding huge per-call overhead that pollutes the measurements. Disable it
// before any benchmarking so we measure marshalling, not logging.
if (typeof __disableVerboseLogging === "function") {
    __disableVerboseLogging();
}

const benchmarkRunner = require("./marshalling-benchmark.js");

// Results are streamed to logcat, tagged NS_ENGINE_BENCHMARK, with a final
// NS_ENGINE_BENCHMARK_DONE line (see marshalling-benchmark.js). The host reads
// them live from adb logcat — no files needed.
self.onmessage = function () {
    try {
        const result = benchmarkRunner.runBenchmark();
        if (typeof globalThis.gc === "function") {
            globalThis.gc();
        }
        self.postMessage(result);
    } catch (e) {
        const msg = "Marshalling benchmark failed: " + (e && e.stack ? e.stack : e);
        console.log("NS_ENGINE_BENCHMARK_DONE error=" + msg);
        self.postMessage(msg);
    }
};
