# documentation: https://d.android.com/studio/projects/add-native-code.html

# Command info: https://cmake.org/cmake/help/v3.4/command/cmake_minimum_required.html
cmake_minimum_required(VERSION 3.4.1)

project(NativeScriptAndroidRuntime)

# Command info: https://cmake.org/cmake/help/v3.4/command/message.html
# we pass the android_ndk_root from gradle because for some reason
# "-DANDROID_STL=c++_static" is just not enough for clang++ to find some libraries in the ndk
MESSAGE(STATUS "## ANDROID_NDK_ROOT: " ${ANDROID_NDK_ROOT})

set(COMMON_CMAKE_ARGUMENTS "-std=c++17 -Werror -Wno-unused-result -mstackrealign -fexceptions -fno-builtin-stpcpy -fno-rtti")

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
        # node api
        src/main/cpp/napi
        # quickjs engine
        src/main/cpp/quickjs
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
        src/main/cpp/runtime/version
)

if (OPTIMIZED_BUILD OR OPTIMIZED_WITH_INSPECTOR_BUILD)
    set(CMAKE_CXX_FLAGS "${COMMON_CMAKE_ARGUMENTS} -O3 -fvisibility=hidden -ffunction-sections -fno-data-sections")
else ()
    set(CMAKE_CXX_FLAGS "${COMMON_CMAKE_ARGUMENTS} -g")
endif ()

# Search for all CPP files in runtime/ directory and add them to our sourcess
file(GLOB_RECURSE RUNTIME_FILES
        "${PROJECT_SOURCE_DIR}/src/main/cpp/runtime/*.cpp"
        "${PROJECT_SOURCE_DIR}/src/main/cpp/runtime/**/*.cpp"
)

# Command info: https://cmake.org/cmake/help/v3.4/command/add_library.html
# Creates(shared static) and names a library given relative sources
# Gradle automatically packages shared libraries with your APK.
add_library(
        # Sets the name of the library. When it's built you can find it with lib prefix libNativeScript.so
        NativeScript

        # Sets the library as a shared library.
        SHARED

         # quickjs
         src/main/cpp/quickjs/cutils.c
         src/main/cpp/quickjs/libregexp.c
         src/main/cpp/quickjs/libbf.c
         src/main/cpp/quickjs/libunicode.c
         src/main/cpp/quickjs/quickjs.c
         # napi
         src/main/cpp/napi/native_api.cpp
         # runtime
         ${RUNTIME_FILES}
)


set_target_properties(
            NativeScript
            PROPERTIES LINK_FLAGS -Wl,--allow-multiple-definition
)

MESSAGE(STATUS "# General cmake Info")
MESSAGE(STATUS "# PROJECT_SOURCE_DIR: " ${PROJECT_SOURCE_DIR})
MESSAGE(STATUS "# CMAKE_VERSION: " ${CMAKE_VERSION})
MESSAGE(STATUS "# CMAKE_C_COMPILER_ID: " ${CMAKE_C_COMPILER_ID})
MESSAGE(STATUS "# CMAKE_CXX_COMPILER_ID: " ${CMAKE_CXX_COMPILER_ID})
MESSAGE(STATUS "# CMAKE_C_FLAGS: " ${CMAKE_C_FLAGS})
MESSAGE(STATUS "# CMAKE_CXX_FLAGS: " ${CMAKE_CXX_FLAGS})

# Command info: https://cmake.org/cmake/help/v3.4/command/target_link_libraries.html
# linking custom STL libraries to the runtime (NativeScript library)
target_link_libraries(NativeScript ${PROJECT_SOURCE_DIR}/src/main/libs/${ANDROID_ABI}/libzip.a)

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
target_link_libraries(NativeScript ${system-log} ${system-z} ${system-android})