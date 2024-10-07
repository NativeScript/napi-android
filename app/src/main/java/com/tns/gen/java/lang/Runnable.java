package com.tns.gen.java.lang;

public class Runnable extends java.lang.Object
    implements org.nativescript.runtime.napi.NativeScriptHashCodeProvider, java.lang.Runnable {
  public Runnable() {
    super();
    org.nativescript.runtime.napi.Runtime.initInstance(this);
  }

  public void run() {
    java.lang.Object[] args = new java.lang.Object[0];
    org.nativescript.runtime.napi.Runtime.callJSMethod(this, "run", void.class, args);
  }

  public int hashCode__super() {
    return super.hashCode();
  }

  public boolean equals__super(java.lang.Object other) {
    return super.equals(other);
  }
}
