package org.nativescript.runtime.napi;

import android.os.Handler;

public interface ThreadScheduler {
    boolean post(Runnable r);

    Thread getThread();

    Handler getHandler();
}
