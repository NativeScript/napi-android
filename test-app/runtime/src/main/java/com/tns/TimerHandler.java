package com.tns;

import android.os.Handler;
import android.os.Looper;
import android.os.Message;

/**
 * Delivers native timer callbacks through the runtime thread's Java MessageQueue
 * (instead of an ALooper fd), so setTimeout/setInterval share a single queue with
 * Handler.post/postDelayed and fire in exact MessageQueue order. Each scheduled
 * timer enqueues one anonymous "due token" message; native picks the earliest-due
 * timer when the token is handled.
 */
final class TimerHandler extends Handler {
    private static final int MSG_FIRE_TIMER = 1;

    private final long nativeTimersPtr;
    private boolean released;

    // constructed from native code (Timers::Init) on the runtime thread
    TimerHandler(long nativeTimersPtr) {
        super(Looper.myLooper());
        this.nativeTimersPtr = nativeTimersPtr;
    }

    @RuntimeCallable
    void post(long uptimeMillis) {
        sendMessageAtTime(obtainMessage(MSG_FIRE_TIMER), uptimeMillis);
    }

    @RuntimeCallable
    void release() {
        released = true;
        removeCallbacksAndMessages(null); // safe: this handler is timers-only
    }

    @Override
    public void handleMessage(Message msg) {
        if (released || msg.what != MSG_FIRE_TIMER) {
            return;
        }
        nativeFireTimer(nativeTimersPtr);
    }

    private static native void nativeFireTimer(long nativeTimersPtr);
}
