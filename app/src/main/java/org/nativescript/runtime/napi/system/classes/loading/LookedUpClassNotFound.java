package org.nativescript.runtime.napi.system.classes.loading;

public class LookedUpClassNotFound extends RuntimeException {

    private static final long serialVersionUID = 123321L;

    public LookedUpClassNotFound(String message) {
        super(message);
    }
}
