# documentation: https://d.android.com/studio/projects/add-native-code.html

# Command info: https://cmake.org/cmake/help/v3.4/command/cmake_minimum_required.html
cmake_minimum_required(VERSION 3.4.1)

project(NativeScriptAndroidRuntime)

# Command info: https://cmake.org/cmake/help/v3.4/command/message.html
# we pass the android_ndk_root from gradle because for some reason
# "-DANDROID_STL=c++_static" is just not enough for clang++ to find some libraries in the ndk
MESSAGE(STATUS "## ANDROID_NDK_ROOT: " ${ANDROID_NDK_ROOT})

# Add the ccache to the build system
find_program(CCACHE_FOUND ccache)
if (CCACHE_FOUND AND (USE_CCACHE))
    MESSAGE(STATUS "## Using CCache when building!")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif (CCACHE_FOUND AND (USE_CCACHE))

set(COMMON_CMAKE_ARGUMENTS "-std=c++17 -Wno-error -Wno-deprecated-declarations -Wno-unused-result -mstackrealign -fexceptions -fno-builtin-stpcpy -fno-rtti")
set(MI_OVERRIDE OFF)
# AOSP has switched to using LLD by default and the NDK will use it by default in the next release.
# BFD and Gold will be removed once LLD has been through a release cycle with no major unresolved issues (estimated r21)
# Note: lld does not currently work on Windows: https://github.com/android-ndk/ndk/issues/888

# On MacOS using LLD seems problematic as it does not add the correct path for the libNativeScript.so dSYM.
# This issue affects debugging the C++ part of the runtime.
# Manually performing "add-dsym <lib-path>" in the LLDB console seems to fix that.
# We should try using LLD again once it's the default linker for the NDK.

#if (NOT CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
#  MESSAGE(STATUS "## Using LLD linker")
#  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=lld")
#else ()
#  MESSAGE(STATUS "## Using default linker")
#endif ()

#add_library( v8_shared STATIC IMPORTED )
#set_target_properties( v8_shared PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/src/main/libs/${ANDROID_ABI}/libv8android.so )

# Command info: https://cmake.org/cmake/help/v3.4/command/include_directories.html
include_directories(
        # zip
        src/main/cpp/zip/include
        # runtime
        src/main/cpp/runtime
        src/main/cpp/runtime/assetextractor
        src/main/cpp/runtime/callbackhandlers
        src/main/cpp/runtime/console
        src/main/cpp/runtime/constants
        src/main/cpp/runtime/conversion
        src/main/cpp/runtime/exceptions
        src/main/cpp/runtime/global
        src/main/cpp/runtime/instrumentation
        src/main/cpp/runtime/inspector
        src/main/cpp/runtime/jni
        src/main/cpp/runtime/messageloop
        src/main/cpp/runtime/metadata
        src/main/cpp/runtime/module
        src/main/cpp/runtime/objectmanager
        src/main/cpp/runtime/performance
        src/main/cpp/runtime/profiler
        src/main/cpp/runtime/robinhood
        src/main/cpp/runtime/sighandler
        src/main/cpp/runtime/timers
        src/main/cpp/runtime/util
        src/main/cpp/runtime/jsonhelper
        src/main/cpp/runtime/version
        src/main/cpp/runtime/weakref
)

# Search for all CPP files in runtime/ directory and add them to our sourcess
file(GLOB_RECURSE RUNTIME_FILES
        "${PROJECT_SOURCE_DIR}/src/main/cpp/runtime/*.cpp"
        "${PROJECT_SOURCE_DIR}/src/main/cpp/runtime/**/*.cpp"
)

set(SOURCES ${RUNTIME_FILES})

if (QUICKJS)
    add_subdirectory(${PROJECT_SOURCE_DIR}/src/main/cpp/napi/quickjs/mimalloc-dev mimalloc)
    set(SOURCES ${SOURCES}
            # quickjs
            src/main/cpp/napi/quickjs/source/cutils.c
            src/main/cpp/napi/quickjs/source/libregexp.c
            src/main/cpp/napi/quickjs/source/libbf.c
            src/main/cpp/napi/quickjs/source/libunicode.c
            src/main/cpp/napi/quickjs/source/quickjs.c
            # napi
            src/main/cpp/napi/quickjs/quickjs-api.c
            src/main/cpp/napi/quickjs/jsr.cpp

    )
    include_directories(
            src/main/cpp/napi/quickjs
            src/main/cpp/napi/quickjs/source
            src/main/cpp/napi/common
            # mimalloc
            src/main/cpp/napi/quickjs/mimalloc-dev/include
    )

endif ()

if (HERMES)
    include_directories(
         src/main/cpp/napi/hermes
         src/main/cpp/napi/hermes/include
         src/main/cpp/napi/common
    )
    set(SOURCES ${SOURCES}
        src/main/cpp/napi/hermes/jsr.cpp
      )
endif ()

if (JSC)
    include_directories(
            src/main/cpp/napi/jsc
            src/main/cpp/napi/jsc/include
            src/main/cpp/napi/common
    )
    set(SOURCES ${SOURCES}
            src/main/cpp/napi/jsc/jsc-api.cpp
            src/main/cpp/napi/jsc/jsr.cpp
    )
endif ()

if (V8)
    include_directories(
            src/main/cpp/napi/v8
            src/main/cpp/napi/v8/include
            src/main/cpp/napi/common
            src/main/cpp/napi/v8/v8_inspector
    )

    if (NOT OPTIMIZED_BUILD OR OPTIMIZED_WITH_INSPECTOR_BUILD)
        add_definitions(-DAPPLICATION_IN_DEBUG)
        # Debug builds will include the V8 inspector sources
        set(
                SOURCES
                ${SOURCES}
                src/main/cpp/napi/v8/v8_inspector/Utils.cpp
                src/main/cpp/napi/v8/v8_inspector/ns-v8-tracing-agent-impl.cpp
        )
    endif ()

    set(SOURCES ${SOURCES}
            src/main/cpp/napi/v8/v8-api.cpp
            src/main/cpp/napi/v8/jsr.cpp
            src/main/cpp/napi/v8/SimpleAllocator.cpp
    )

    set(COMMON_CMAKE_ARGUMENTS "${COMMON_CMAKE_ARGUMENTS} -DV8_31BIT_SMIS_ON_64BIT_ARCH -DV8_31BIT_SMIS_ON_64BIT_ARCH -DV8_ENABLE_REGEXP_INTERPRETER_THREADED_DISPATCH -DV8_EMBEDDED_BUILTINS")

    if("${ANDROID_ABI}" MATCHES "arm64-v8a$" OR "${ANDROID_ABI}" MATCHES "x86_64$")
        # Enable pointer compression on 64 bit platforms
        set(COMMON_CMAKE_ARGUMENTS "${COMMON_CMAKE_ARGUMENTS} -DV8_COMPRESS_POINTERS")
    endif()
endif ()


if (OPTIMIZED_BUILD OR OPTIMIZED_WITH_INSPECTOR_BUILD)
    set(CMAKE_CXX_FLAGS "${COMMON_CMAKE_ARGUMENTS} -O3 -flto -fvisibility=hidden -ffunction-sections -fno-data-sections")
else ()
    set(CMAKE_CXX_FLAGS "${COMMON_CMAKE_ARGUMENTS} -g")
endif ()


# Command info: https://cmake.org/cmake/help/v3.4/command/add_library.html
# Creates(shared static) and names a library given relative sources
# Gradle automatically packages shared libraries with your APK.
add_library(
        # Sets the name of the library. When it's built you can find it with lib prefix libNativeScript.so
        NativeScript

        # Sets the library as a shared library.
        SHARED

        ${SOURCES}
)

if (OPTIMIZED_BUILD OR OPTIMIZED_WITH_INSPECTOR_BUILD)
    set_target_properties(
            NativeScript
            PROPERTIES LINK_FLAGS -Wl,--allow-multiple-definition -Wl,--exclude-libs=ALL -Wl,--gc-sections
    )
else ()
    set_target_properties(
            NativeScript
            PROPERTIES LINK_FLAGS -Wl,--allow-multiple-definition
    )
#    target_compile_options(NativeScript PUBLIC -fsanitize=hwaddress -fno-omit-frame-pointer)
#    target_link_options(NativeScript PUBLIC -fsanitize=hwaddress)
endif ()

MESSAGE(STATUS "# General cmake Info")
MESSAGE(STATUS "# PROJECT_SOURCE_DIR: " ${PROJECT_SOURCE_DIR})
MESSAGE(STATUS "# CMAKE_VERSION: " ${CMAKE_VERSION})
MESSAGE(STATUS "# CMAKE_C_COMPILER_ID: " ${CMAKE_C_COMPILER_ID})
MESSAGE(STATUS "# CMAKE_CXX_COMPILER_ID: " ${CMAKE_CXX_COMPILER_ID})
MESSAGE(STATUS "# CMAKE_C_FLAGS: " ${CMAKE_C_FLAGS})
MESSAGE(STATUS "# CMAKE_CXX_FLAGS: " ${CMAKE_CXX_FLAGS})

# Command info: https://cmake.org/cmake/help/v3.4/command/target_link_libraries.html
# linking custom STL libraries to the runtime (NativeScript library)
target_link_libraries(NativeScript ${PROJECT_SOURCE_DIR}/src/main/libs/common/${ANDROID_ABI}/libzip.a)
#target_link_libraries(NativeScript ${PROJECT_SOURCE_DIR}/src/main/libs/common/${ANDROID_ABI}/libclang_rt.asan-aarch64-android.so)

if (HERMES)
    target_link_libraries(NativeScript ${PROJECT_SOURCE_DIR}/src/main/libs/hermes/${ANDROID_ABI}/libhermes.so)
    target_link_libraries(NativeScript ${PROJECT_SOURCE_DIR}/src/main/libs/hermes/${ANDROID_ABI}/libjsi.so)
    add_compile_definitions(NativeScript, PRIVATE __HERMES__)
endif ()

if (JSC)
    target_link_libraries(NativeScript ${PROJECT_SOURCE_DIR}/src/main/libs/jsc/${ANDROID_ABI}/libjsc.so)
    add_compile_definitions(NativeScript, PRIVATE __JSC__)
endif ()

if (V8)
    target_link_libraries(NativeScript ${PROJECT_SOURCE_DIR}/src/main/libs/v8/${ANDROID_ABI}/libv8_monolith.a)
    add_compile_definitions(NativeScript, PRIVATE __V8__)
endif ()

if (QUICKJS)
    add_compile_definitions(NativeScript, PRIVATE __QJS__)
    if (USE_MIMALLOC)
        add_compile_definitions(NativeScript, PRIVATE USE_MIMALLOC)
    endif ()
endif ()

# if("${ANDROID_ABI}" MATCHES "armeabi-v7a$" OR "${ANDROID_ABI}" MATCHES "x86$")
#     # On API Level 19 and lower we need to link with android_support
#     # because it contains some implementation of functions such as "strtoll" and "strtoul"
#     MESSAGE(STATUS "# Linking with libandroid_support.a")
#     target_link_libraries(NativeScript ${ANDROID_NDK_ROOT}/sources/cxx-stl/llvm-libc++/libs/${ANDROID_ABI}/libandroid_support.a)
# endif()

# Command info: https://cmake.org/cmake/help/v3.4/command/find_library.html
# Searches for a specified prebuilt library and stores the path as a
# variable. Because CMake includes system libraries in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build.
find_library(system-log log)
find_library(system-z z)
find_library(system-android android)

# Command info: https://cmake.org/cmake/help/v3.4/command/target_link_libraries.html
# Specifies libraries CMake should link to your target library.
if (QUICKJS)
    if (USE_MIMALLOC)
        target_link_libraries(NativeScript ${system-log} ${system-z} ${system-android} mimalloc-static)
    else ()
        target_link_libraries(NativeScript ${system-log} ${system-z} ${system-android})
    endif ()
elseif (HERMES)
    find_package(fbjni REQUIRED CONFIG)
    target_link_libraries(NativeScript ${system-log} ${system-z} fbjni::fbjni ${system-android})
elseif (JSC OR V8)
    target_link_libraries(NativeScript ${system-log} ${system-z} ${system-android})
endif ()


