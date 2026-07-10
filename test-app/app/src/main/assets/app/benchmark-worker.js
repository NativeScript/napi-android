// Runs the Octane benchmark suite off the main (UI) thread so the app
// stays responsive and Android does not raise an ANR while benchmarking.
const benchmarkRunner = require("./benchmark.js");

self.onmessage = function () {
    try {
        const result = benchmarkRunner.runBenchmark();
        if (typeof globalThis.gc === "function") {
            globalThis.gc();
        }
        self.postMessage(result);
    } catch (e) {
        self.postMessage("Benchmark failed: " + (e && e.stack ? e.stack : e));
    }
};
