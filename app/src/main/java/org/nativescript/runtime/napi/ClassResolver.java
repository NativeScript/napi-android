package org.nativescript.runtime.napi;

import org.nativescript.runtime.napi.system.classes.loading.ClassStorageService;

import java.io.IOException;

class ClassResolver {
    private final ClassStorageService classStorageService;

    ClassResolver(ClassStorageService classStorageService) {
        this.classStorageService = classStorageService;
    }

    Class<?> resolveClass(String baseClassName, String fullClassName, DexFactory dexFactory, String[] methodOverrides, String[] implementedInterfaces, boolean isInterface) throws ClassNotFoundException, IOException {
        String canonicalClassName = fullClassName.replace('/', '.');
        String canonicalBaseClassName = baseClassName.replace('/', '.');
        String name = "";
        String className = canonicalClassName;

        Class<?> clazz = null;
        boolean isBindingClass = canonicalClassName.startsWith("org.nativescript.runtime.napi.gen") && !canonicalClassName.startsWith("org.nativescript.runtime.napi.tests.");

        // if binding generate proxy or load pregenerated
        if (isBindingClass) {
            clazz = dexFactory.resolveClass(canonicalBaseClassName, name, className, methodOverrides, implementedInterfaces, isInterface);
        }

        if (clazz == null) {
            clazz = classStorageService.retrieveClass(className);
        }

        return clazz;
    }
}
