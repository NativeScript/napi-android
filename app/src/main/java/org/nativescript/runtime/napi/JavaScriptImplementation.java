package org.nativescript.runtime.napi;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

@Retention(RetentionPolicy.RUNTIME)
public @interface JavaScriptImplementation {
    String javaScriptFile();
}