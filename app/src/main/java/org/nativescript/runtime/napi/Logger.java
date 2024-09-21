package org.nativescript.runtime.napi;

public interface Logger {
    boolean isEnabled();

    void setEnabled(boolean isEnabled);

    void write(String msg);

    void write(String tag, String msg);
}