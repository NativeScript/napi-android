// Runtime marshalling micro-benchmarks. Exercises every JS <-> Java binding
// path exposed by org.nativescript.Benchmarks and reports per-call cost.
//
// Ported from the standalone NativeScript "TestAppNS" benchmark app so the
// numbers can be tracked against the runtime sources directly. Runs off the UI
// thread via marshalling-benchmark-worker.js.

// Marker prefixed to every result line so the host can filter benchmark output
// straight out of live logcat (adb logcat | grep NS_ENGINE_BENCHMARK).
var MARKER = "NS_ENGINE_BENCHMARK";

// Workers don't get a `performance` global, so read the monotonic clock straight
// from the platform. nanoTime() is unaffected by wall-clock changes.
function now() {
    return java.lang.System.nanoTime() / 1e6; // ms
}

// Runs one entry and returns a formatted result line. `iterations` is the number
// of marshalling calls the action performs; ns/call is derived from it.
function run(entry) {
    var n = entry.iterations || 1e6;
    var start = now();
    entry.action();
    var ms = now() - start;
    var perCall = (ms / n * 1000).toFixed(1);
    return {
        ms: ms,
        line: pad(ms.toFixed(2), 9) + " ms  [" + perCall + " ns/call]  " + entry.name,
    };
}

function pad(str, width) {
    str = String(str);
    while (str.length < width) {
        str = " " + str;
    }
    return str;
}

function buildSuite() {
    var Benchmarks = org.nativescript.Benchmarks;
    var b = new org.nativescript.Benchmarks();

    return [
        { name: "Void Method on instance",    action: function () { for (var i = 0; i < 1e6; i++) b.voidMethod1(); } },
        { name: "Field on instance",          action: function () { for (var i = 0; i < 1e6; i++) void b.InstanceField; } },
        { name: "Int Field on instance",      action: function () { for (var i = 0; i < 1e6; i++) void b.IntFieldInstance; } },
        { name: "Get Static Field",           action: function () { for (var i = 0; i < 1e6; i++) void Benchmarks.Field; } },
        { name: "Void Static Method",         action: function () { for (var i = 0; i < 1e6; i++) Benchmarks.voidMethod(); } },
        { name: "multiply(a, b)",             action: function () { for (var i = 0; i < 1e6; i++) Benchmarks.multiply(3, 2); } },
        { name: "Pass and return string",     action: function () { for (var i = 0; i < 1e6; i++) Benchmarks.passAndReturnString("hello world"); } },
        { name: "Return a String",            action: function () { for (var i = 0; i < 1e6; i++) void Benchmarks.returnString(); } },
        { name: "Return an Int",              action: function () { for (var i = 0; i < 1e6; i++) void Benchmarks.returnInt(); } },
        { name: "Pass a String",              action: function () { for (var i = 0; i < 1e6; i++) Benchmarks.passString("test"); } },
        { name: "Return a Boolean",           action: function () { for (var i = 0; i < 1e6; i++) void Benchmarks.returnBoolean(); } },
        { name: "Return a Double",            action: function () { for (var i = 0; i < 1e6; i++) void Benchmarks.returnDouble(); } },
        { name: "Pass an Int",                action: function () { for (var i = 0; i < 1e6; i++) Benchmarks.passInt(10); } },
        { name: "Pass a Double",              action: function () { for (var i = 0; i < 1e6; i++) Benchmarks.passDouble(10.5); } },
        { name: "Pass a Boolean",             action: function () { for (var i = 0; i < 1e6; i++) Benchmarks.passBoolean(true); } },

        // Array benchmarks — each call allocates/marshals a Java array, so fewer iterations.
        { name: "Return an Int Array",        iterations: 1e4, action: function () { for (var i = 0; i < 1e4; i++) void Benchmarks.returnIntArray(); } },
        { name: "Pass an Int Array",          iterations: 1e4, action: function () { for (var i = 0; i < 1e4; i++) Benchmarks.passIntArray([1, 2, 3, 4, 5]); } },
        { name: "Return a String Array",      iterations: 1e4, action: function () { for (var i = 0; i < 1e4; i++) void Benchmarks.returnStringArray(); } },
        { name: "Pass a String Array",        iterations: 1e4, action: function () { for (var i = 0; i < 1e4; i++) Benchmarks.passStringArray(["one", "two", "three"]); } },
        { name: "Return a Double Array",      iterations: 1e4, action: function () { for (var i = 0; i < 1e4; i++) void Benchmarks.returnDoubleArray(); } },
        { name: "Pass a Double Array",        iterations: 1e4, action: function () { for (var i = 0; i < 1e4; i++) Benchmarks.passDoubleArray([1.1, 2.2, 3.3]); } },
        { name: "Return a Boolean Array",     iterations: 1e4, action: function () { for (var i = 0; i < 1e4; i++) void Benchmarks.returnBooleanArray(); } },
        { name: "Pass a Boolean Array",       iterations: 1e4, action: function () { for (var i = 0; i < 1e4; i++) Benchmarks.passBooleanArray([true, false, true]); } },

        // Indexed access on Java arrays — array fetched once, only element access is measured.
        { name: "Int Array[0] read",          iterations: 1e6, action: function () { var a = Benchmarks.returnIntArray();     for (var i = 0; i < 1e6; i++) void a[0]; } },
        { name: "String Array[0] read",       iterations: 1e6, action: function () { var a = Benchmarks.returnStringArray();  for (var i = 0; i < 1e6; i++) void a[0]; } },
        { name: "Double Array[0] read",       iterations: 1e6, action: function () { var a = Benchmarks.returnDoubleArray();  for (var i = 0; i < 1e6; i++) void a[0]; } },
        { name: "Boolean Array[0] read",      iterations: 1e6, action: function () { var a = Benchmarks.returnBooleanArray(); for (var i = 0; i < 1e6; i++) void a[0]; } },
        { name: "Int Array[0] write",         iterations: 1e6, action: function () { var a = Benchmarks.returnIntArray();     for (var i = 0; i < 1e6; i++) a[0] = 42; } },
        { name: "String Array[0] write",      iterations: 1e6, action: function () { var a = Benchmarks.returnStringArray();  for (var i = 0; i < 1e6; i++) a[0] = "x"; } },
        { name: "Double Array[0] write",      iterations: 1e6, action: function () { var a = Benchmarks.returnDoubleArray();  for (var i = 0; i < 1e6; i++) a[0] = 1.5; } },
        { name: "Boolean Array[0] write",     iterations: 1e6, action: function () { var a = Benchmarks.returnBooleanArray(); for (var i = 0; i < 1e6; i++) a[0] = false; } },

        // Object and callback marshalling — allocation-heavy, low iteration count.
        { name: "Return a Date Object",       iterations: 2000, action: function () { for (var i = 0; i < 2000; i++) void Benchmarks.returnDateObject(); } },
        { name: "Pass a Date Object",         iterations: 2000, action: function () { var d = new java.util.Date(); for (var i = 0; i < 2000; i++) Benchmarks.passDateObject(d); } },
        { name: "Invoke void callback",       iterations: 2000, action: function () {
            var cb = new org.nativescript.BenchmarkCallback({ onCallback: function () {} });
            for (var i = 0; i < 2000; i++) Benchmarks.invokeCallback(cb);
        } },
    ];
}

// Runs the full suite and returns a formatted report string.
function runBenchmark() {
    var suite = buildSuite();
    var lines = [];
    var totalMs = 0;

    // Every line is tagged MARKER so the host can filter it out of logcat live:
    //   adb logcat | grep NS_ENGINE_BENCHMARK
    // The final NS_ENGINE_BENCHMARK_DONE line signals completion immediately.
    var engine = typeof __engine === "string" ? __engine : "V8";

    for (var i = 0; i < suite.length; i++) {
        var entry = suite[i];
        try {
            var result = run(entry);
            totalMs += result.ms;
            lines.push(result.line);
            console.log(MARKER + " " + result.line);
        } catch (e) {
            var failLine = "  FAILED  " + entry.name + ": " + (e && e.stack ? e.stack : e);
            lines.push(failLine);
            console.log(MARKER + " " + failLine);
        }
        // Yield/GC hint between entries to keep allocation noise out of neighbours.
        if (typeof globalThis.gc === "function") {
            globalThis.gc();
        }
    }

    console.log(MARKER + "_DONE engine=" + engine + " total_ms=" + totalMs.toFixed(2) + " entries=" + suite.length);

    var summary = [
        "",
        "----------------------------------------------------",
        "Engine : " + engine,
        "Total  : " + totalMs.toFixed(2) + " ms  (lower = better)",
    ].join("\n");

    return lines.join("\n") + summary;
}

exports.runBenchmark = runBenchmark;
