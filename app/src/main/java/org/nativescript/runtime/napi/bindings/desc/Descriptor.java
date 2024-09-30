package org.nativescript.runtime.napi.bindings.desc;

public interface Descriptor {
    boolean isSynthetic();

    boolean isStatic();

    boolean isPrivate();

    boolean isPublic();

    boolean isProtected();

    boolean isFinal();

    boolean isAbstract();
}
