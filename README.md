# TDLib

This repository contains the code of the Telegram Database library (`TDLib`).

## Table of Content
- [About](#about)
- [Features](#features)
- [License](#license)
- [Dependencies](#dependencies)
- [Build with cmake](#build-cmake)
- [Using `TDLib` in CMake C++ projects](#using-cmake)
- [Using `TDLib` with other programming languages](#using-json)
- [Installing dependencies](#installing-dependencies)
- [Usage](#usage)

<a name="about"></a>
## About

`TDLib` is a cross-platform, fully functional Telegram client.

<a name="features"></a>
## Features

`TDLib` has many advantages. Notably `TDLib` is:

* **Cross-platform**: `TDLib` can be used on Android, iOS, Windows, macOS, Linux, Windows Phone, WebAssembly, watchOS, tvOS, Tizen, Cygwin. It should also work on other *nix systems with or without minimal effort.
* **Multilanguage**: `TDLib` can be easily used with any programming language that is able to execute C functions. Additionally it already has native bindings to Java (using JNI) and C# (using C++/CLI).
* **Easy to use**: `TDLib` takes care of all network implementation details, encryption and local data storage.
* **High-performance**: in the Telegram Bot API, each `TDLib` instance handles more than 18000 active bots simultaneously.
* **Well-documented**: all `TDLib` API methods and public interfaces are fully documented.
* **Consistent**: `TDLib` guarantees that all updates will be delivered in the right order.
* **Reliable**: `TDLib` remains stable on slow and unstable Internet connections.
* **Secure**: all local data is encrypted using a user-provided encryption key.
* **Fully-asynchronous**: requests to `TDLib` don't block each other or anything else, responses will be sent when they are available.

<a name="license"></a>
## License 
The Telegram Database library is licensed under the terms of the
Boost Software License. See [LICENSE](http://www.boost.org/LICENSE_1_0.txt) for more information.

## Build

<a name="dependencies"></a>
### Dependencies
`TDLib` depends on:

* C++14 compatible compiler (clang 3.4+, GCC 4.9+, MSVC 19.0+ (Visual Studio 2015+), Intel C++ Compiler 17+)
* OpenSSL
* zlib
* gperf
* CMake (3.0.2+)
* php (optional, for docs generation)
* doxygen (optional, for docs generation)

<a name="build-cmake"></a>
### Build with CMake

```
mkdir build
cd build
cmake ..
cmake --build .
cmake --build . --target install
```

<a name="using-cmake"></a>
### Using `TDLib` in CMake C++ projects
For C++ projects that use CMake, the best approach is to build `TDLib` as part of your project.

There are several libraries that you could use in your CMake project:

* Td::TdJson, Td::TdJsonStatic — dynamic and static version of a json interface. Has a simple C interface, so it can be easily used with any language that supports C bindings.
* Td::TdStatic — static library with C++ interface.
* Td::TdCoreStatic — static library with low-level C++ interface intended mostly for internal usage.

For example, part of your CMakeList.txt may look like this:
```
add_subdirectory(td)
target_link_library(YourLibrary Td::TdJson)
```

Or you could install `TDLib` and then reference it in your CMakeLists.txt like this:
```
find_package(Td 1.0)
target_link_library(YourLibrary Td::TdJson)
```
See [example/cpp/CMakeLists.txt](https://github.com/tdlib/td/tree/master/example/cpp/CMakeLists.txt).

<a name="using-json"></a>
### Using `TDLib` from other languages
`TDLib` provides efficient native C++, Java, and C# (will be released soon) interfaces.
But for most use cases we suggest to use the JSON interface. It can be easily used with any language that supports C bindinds. See
[example/python/tdjson_example.py](https://github.com/tdlib/td/tree/master/example/python/tdjson_example.py) for an
example of such usage.

<a name="installing-dependencies"></a>
### Installing dependencies

#### OS X
* Install the latest XCode command line tools.
* Install other dependencies, for example, using [Homebrew](https://brew.sh):
```
brew install gperf cmake openssl
```
* After that you may need to manually specify path to the installed OpenSSL to CMake, e.g.,
```
cmake -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl/ ..
```

#### Windows
* Download and install [gperf](https://sourceforge.net/projects/gnuwin32/files/gperf/3.0.1/). Add the path to gperf to the PATH variable.
* Install [vcpkg](https://github.com/Microsoft/vcpkg#quick-start).
* Run the following commands:
```
C:\src\vcpkg> .\vcpkg install openssl zlib
```
* Build `TDLib` with cmake as explained above, but instead of `cmake ..` use `cmake -DCMAKE_TOOLCHAIN_FILE=C:\src\vcpkg\scripts\buildsystems\vcpkg.cmake ..`.

#### Linux
Install all dependencies using your package manager.

<a name="usage"></a>
### `TDLib` usage
Take a look at our [examples](https://github.com/tdlib/td/tree/master/example) and [documentation](https://core.telegram.org/tdlib/docs/).
