<p align="center" >
  <img src="https://raw.githubusercontent.com/NativeScript/artwork/refs/heads/main/logo/export/NativeScript_Logo_White_Blue_Rounded.png" alt="NativeScript Logo" width="20%" height="20%">
</p>

<h1 align="center">Node-API Android Runtime for NativeScript</h1>



[NativeScript](https://www.nativescript.org/) is an open-source framework for building truly native mobile applications using JavaScript. This repository contains the source code for the Node-API based Android Runtime used by NativeScript.

The Android Runtime is a key component of the NativeScript framework. It is responsible for executing JavaScript code on Android devices. The runtime is built on top of [Node-API](https://nodejs.org/api/n-api.html) and provides a way to interact with the Android platform APIs from JavaScript.

The new runtime is based on the Node-API and is designed to be more stable, faster, and easier to maintain. It also supports multiple JavaScript engines, including [V8](https://v8.dev/), [QuickJS](https://github.com/quickjs-ng/quickjs/), [Hermes](https://github.com/facebook/hermes), and [JavaScriptCore](https://docs.webkit.org/Deep%20Dive/JSC/JavaScriptCore.html).

<!-- TOC depthFrom:2 -->

- [Main Projects](#main-projects)
- [Helper Projects](#helper-projects)
- [Build Prerequisites](#build-prerequisites)
- [How to build](#how-to-build)
- [How to run tests](#how-to-run-tests)
- [Misc](#misc)
- [Get Help](#get-help)

<!-- /TOC -->

## Project structure

The repo is structured in the following projects (ordered by dependencies):

- [**android-metadata-generator**](android-metadata-generator) - generates metadata necessary for the Android Runtime.
- [**android-binding-generator**](test-app/runtime-binding-generator) - enables Java & Android types to be dynamically created at runtime. Needed by the `extend` routine.
- [**android-runtime**](test-app/runtime) - contains the core logic behind the NativeScript's Android Runtime. This project contains native C++ code and needs the Android NDK to build properly.
- [**android-runtime-testapp**](test-app/app) - this is a vanilla Android Application, which contains the tests for the runtime project.
- [**napi-implementations**](test-app/runtime/src/main//cpp/napi/) - contains the implementation of the Node-API for each supported JS engine.

## Helper Projects

- [**android-static-binding-generator**](android-static-binding-generator) - build tool that generates bindings based on the user's javascript code.
- [**project-template**](build-artifacts/project-template-gradle) - this is an empty placeholder Android Application project, used by the [NativeScript CLI](https://github.com/NativeScript/nativescript-cli) when building an Android project.

### Build Prerequisites

Following are the minimal prerequisites to build the runtime package.

- Install the latest [Android Studio](https://developer.android.com/studio/index.html).
- From the SDK Manager (Android Studio -> Tools -> Android -> SDK Manager) install the following components:

  - Android API Level 23, 24, 25, 26, 27
  - Android NDK
  - Android Support Repository
  - Download Build Tools
  - CMake
  - LLDB

## How to Build

Clone the repo:

```Shell
git clone https://github.com/NativeScript/napi-android.git
```

Install the jsparser dependencies:

```Shell
cd test-app/build-tools/jsparser
npm install
```

Set the following environment variables:

- `JAVA_HOME` such that `$JAVA_HOME/bin/java` points to your Java executable
- `ANDROID_HOME` pointing to where you have installed the Android SDK
- `ANDROID_NDK_HOME` pointing to the version of the Android NDK needed for this version of NativeScript

Run command

Windows:

```Shell
gradlew -Pengine=V8
```

Mac/Linux:

```Shell
./gradlew -Pengine=V8
```

You can pass in `QUICKJS`, `HERMES`, `JSC` or `V8` to compile the runtime with the respective JS engine.

- The build process includes building of the runtime package (both optimized and with unstripped v8 symbol table), as well as all supplementary tools used for the android builds: metadata-generator, binding-generator, metadata-generator, static-binding-generator
- The result of the build will be in the dist\_[engine] folder. For example if you are building with V8, the result will be in the dist_v8 folder.

  `Note:` To cut the build time in half and package only the optimized (stripped) version of the runtime package comment out 'tasks.generateRuntimeAar.execute()' in the [build.gradle](https://github.com/NativeScript/android-runtime/blob/v3.0.0-rc.1/build.gradle#L114) script.

## How to Run Tests

- Go to subfolder test-app after you built the runtime.
- Start an emulator or connect a device.

- Run command

```Shell
gradlew runtests
```

## Working with the Runtime in Android Studio

- Open the test-app folder in Android Studio. It represents a valid Android project and you are able to build and run a test application working with the Runtime from the source.

Note: You might need to run the Android Studio from the command line in order to preserve the environment variables. This is in case you get errors like "missing npm" if starting the studio the usual way.

You can change the JS engine used by the runtime by setting the `jsEngine` property in the [`build.gradle`](test-app/runtime/build.gradle) file in the root of the project. The possible values are `QUICKJS`, `HERMES`, `JSC` or `V8`.

## Contribute

We love PRs! Check out the [contributing guidelines](CONTRIBUTING.md). If you want to contribute, but you are not sure where to start - look for [issues labeled `help wanted`](https://github.com/NativeScript/android-runtime/issues?q=is%3Aopen+is%3Aissue+label%3A%22help+wanted%22).

## Get Help

Please, use [github issues](https://github.com/NativeScript/android-runtime/issues) strictly for [reporting bugs](CONTRIBUTING.md#reporting-bugs) or [requesting features](CONTRIBUTING.md#requesting-new-features). For general questions and support, check out [Stack Overflow](https://stackoverflow.com/questions/tagged/nativescript) or ask our experts in [NativeScript community Slack channel](http://developer.telerik.com/wp-login.php?action=slack-invitation).

## License

This project is licensed under the Apache License Version 2.0. See the [LICENSE file](LICENSE) for more info.
