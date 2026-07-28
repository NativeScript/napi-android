describe("Runtime exposes", function () {
  it("__time a low overhead, high resolution, time in ms.", function() {
    // __time() must track the same wall clock as Date.now(): measure the same
    // interval with both and expect the two deltas to agree.
    //
    // This is retried across several attempts because the comparison is sensitive
    // to timing noise: the interval is only a few ms, both clocks are ms-granular,
    // and a GC (or scheduler hiccup) between the two samples can skew one delta.
    // Two things matter for the retry to actually work:
    //   - the inner loop must NOT reuse the outer counter `i` (that would clobber
    //     it to the loop bound and run a single attempt), and
    //   - the pass/fail must be accumulated and asserted ONCE at the end, because
    //     a failing expect() records a failure rather than throwing, so asserting
    //     inside the loop can never be "retried away".
    var passed = false;
    for (var i = 0; i < 10 && !passed; i++) {
      var dateTimeStart = Date.now();
      var timeStart = __time();
      var acc = 0;
      var s = android.os.SystemClock.elapsedRealtime();
      for (var j = 0; j < 1000; j++) {
        var c = android.os.SystemClock.elapsedRealtime();
        acc += (c - s);
        s = c;
      }
      var dateTimeEnd = Date.now();
      var timeEnd = __time();
      var dateDelta = dateTimeEnd - dateTimeStart;
      var timeDelta = timeEnd - timeStart;

      // Allow the larger of 25% of the interval or 2ms of slack to absorb the
      // unavoidable ±1ms quantization on each of the two ms-granular clocks.
      var tolerance = Math.max(dateDelta * 0.25, 2);
      if (Math.abs(dateDelta - timeDelta) <= tolerance) {
        passed = true;
      }
    }
    expect(passed).toBe(true);
  });
});