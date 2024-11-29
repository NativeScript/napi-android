if (global.android) {
    (function() {
        const looper = android.os.Looper.myLooper();
            const handler = new android.os.Handler(looper);
            const callback = new java.lang.Runnable({
                run: () => {}
            });
            handler.postDelayed(callback, 1000);
            handler.removeCallbacks(callback);
    })();
} else {
    const o = NSNotificationCenter.defaultCenter.addObserverForNameObjectQueueUsingBlock('send-to-worker', null, null, (obj) => {});
    NSNotificationCenter.defaultCenter.removeObserver(o);
    //__releaseNativeCounterpart(o); // before the fix it crashed without this release
}

onmessage = function(msg) {
    eval(msg.data.eval || "");
}
