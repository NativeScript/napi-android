# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html.
# For more examples on how to use CMake, see https://github.com/android/ndk-samples.

# Sets the minimum CMake version required for this project.
cmake_minimum_required(VERSION 3.22.1)

# Declares the project name. The project name can be accessed via ${ PROJECT_NAME},
# Since this is the top level CMakeLists.txt, the project name is also accessible
# with ${CMAKE_PROJECT_NAME} (both CMake variables are in-sync within the top level
# build script scope).
project("napi-runtime")


# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.
#
# In this top level CMakeLists.txt, ${CMAKE_PROJECT_NAME} is used to define
# the target library name; in the sub-module's CMakeLists.txt, ${PROJECT_NAME}
# is preferred for the same purpose.
#
# In order to load a library into your app from Java/Kotlin, you must call
# System.loadLibrary() and pass the name of the library defined here;
# for GameActivity/NativeActivity derived applications, the same library name must be
# used in the AndroidManifest.xml file.

include_directories(
        ${CMAKE_SOURCE_DIR}/napi
        ${CMAKE_SOURCE_DIR}/quickjs
        ${CMAKE_SOURCE_DIR}/runtime
        ${CMAKE_SOURCE_DIR}/zip/include
        ${CMAKE_SOURCE_DIR}/runtime/jni/robinhood
        ${CMAKE_SOURCE_DIR}/runtime/jni
        ${CMAKE_SOURCE_DIR}/runtime/metadata
)

# Search for all CPP files in runtime/ directory and add them to our sources
file(GLOB_RECURSE RUNTIME_FILES
        "${CMAKE_SOURCE_DIR}/runtime/*.cpp"
        "${CMAKE_SOURCE_DIR}/runtime/jni/*.cpp"
        "${CMAKE_SOURCE_DIR}/runtime/metadata/*.cpp"
)

# Creates and names a library, sets it as either STATIC or SHARED, and provides the relative paths to its source code.
add_library(${CMAKE_PROJECT_NAME} SHARED
        # quickjs
        quickjs/cutils.c
        quickjs/libregexp.c
        quickjs/libbf.c
        quickjs/libunicode.c
        quickjs/quickjs.c
        # napi
        napi/native_api.cpp
        # test
        Init.cpp
        # napi-runtime
        ${RUNTIME_FILES}
)

# Link libzip
target_link_libraries(${CMAKE_PROJECT_NAME} ${PROJECT_SOURCE_DIR}/zip/libs/${ANDROID_ABI}/libzip.a)

# Specifies libraries CMake should link to your target library. You
# can link libraries from various origins, such as libraries defined in this
# build script, prebuilt third-party libraries, or Android system libraries.

find_library(system-z z)

target_link_libraries(${CMAKE_PROJECT_NAME}
        # List libraries link to the target library
        android
        ${system-z}
        log)