package com.tns;

import android.os.Handler;
import android.os.Looper;
import android.os.Message;

/**
 * Drains the runtime's deferred-finalizer queue on the runtime thread's message
 * loop.
 *
 * Native finalizers run during the JS engine's GC, where deleting JS references
 * (or making any other JS/GC-state-changing call) is unsafe on every engine.
 * Such finalizers instead enqueue their cleanup in the native FinalizerQueue and
 * call {@link #schedule()}; this handler then drains that queue at the next loop
 * tick — a point guaranteed to be outside any GC sweep, with JS unwound to the
 * host. This mirrors the engine-specific node_api_post_finalizer but works
 * uniformly for every engine the runtime embeds.
 */
final class FinalizerHandler extends Handler {
    private static final int MSG_DRAIN = 1;

    private final long nativeQueuePtr;
    private boolean released;

    // constructed from native code (FinalizerQueue) on the runtime thread
    FinalizerHandler(long nativeQueuePtr) {
        super(Looper.myLooper());
        this.nativeQueuePtr = nativeQueuePtr;
    }

    // Requests a drain on the next loop tick. Coalesced to at most one in-flight
    // message. Safe to call from any thread (Handler messaging is thread-safe),
    // which matters because GC finalizers may run on a background JS thread.
    @RuntimeCallable
    void schedule() {
        if (released) {
            return;
        }
        if (!hasMessages(MSG_DRAIN)) {
            sendMessage(obtainMessage(MSG_DRAIN));
        }
    }

    @RuntimeCallable
    void release() {
        released = true;
        removeCallbacksAndMessages(null);
    }

    @Override
    public void handleMessage(Message msg) {
        if (released || msg.what != MSG_DRAIN) {
            return;
        }
        nativeDrainFinalizers(nativeQueuePtr);
    }

    private static native void nativeDrainFinalizers(long nativeQueuePtr);
}
