// Time To Interactive (TTI) on app launch.
//
// Measures the wall time from process start to the moment the app becomes
// interactive (the runtime has booted, the main JS module has executed, and the
// first activity has finished building its UI). This captures the full cold-start
// cost the user perceives: native runtime init + JS engine warm-up + metadata
// resolution + app JS execution.
//
// Both endpoints use SystemClock.uptimeMillis() so the delta excludes deep-sleep
// and is immune to wall-clock adjustments. android.os.Process.getStartUptimeMillis()
// (API 24+) is the process fork time on the same clock.

function processStartUptimeMs() {
    // API 24+. Falls back to app-class-load time captured below if unavailable.
    try {
        return android.os.Process.getStartUptimeMillis();
    } catch (e) {
        return -1;
    }
}

// Report TTI. `label` describes the interactive milestone that was just reached.
// Returns the measured value in ms (or -1 if it could not be computed).
function reportTTI(label) {
    var startMs = processStartUptimeMs();
    var nowMs = android.os.SystemClock.uptimeMillis();

    if (startMs < 0) {
        console.log("[TTI] process start time unavailable (API < 24) — cannot measure launch TTI");
        return -1;
    }

    var ttiMs = nowMs - startMs;
    var engine = typeof __engine === "string" ? __engine : "V8";
    // Tagged so the host can read it straight from live logcat.
    console.log("NS_ENGINE_BENCHMARK_TTI engine=" + engine + " tti_ms=" + ttiMs + " label=" + (label || "interactive"));
    return ttiMs;
}

exports.reportTTI = reportTTI;
