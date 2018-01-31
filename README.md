# TDLib

TDLib (Telegram Database library) is a cross-platform library for building [Telegram](https://telegram.org) clients. It can be easily used from almost any programming language.

## Table of Contents
- [Features](#features)
- [Examples and documentation](#usage)
- [Dependencies](#dependencies)
- [Building](#building)
- [Installing dependencies](#installing-dependencies)
- [Using in CMake C++ projects](#using-cxx)
- [Using in Java projects](#using-java)
- [Using with other programming languages](#using-json)
- [License](#license)

<a name="features"></a>
## Features

`TDLib` has many advantages. Notably `TDLib` is:

* **Cross-platform**: `TDLib` can be used on Android, iOS, Windows, macOS, Linux, Windows Phone, WebAssembly, watchOS, tvOS, Tizen, Cygwin. It should also work on other *nix systems with or without minimal effort.
* **Multilanguage**: `TDLib` can be easily used with any programming language that is able to execute C functions. Additionally it already has native bindings to Java (using JNI) and C# (using C++/CLI).
* **Easy to use**: `TDLib` takes care of all network implementation details, encryption and local data storage.
* **High-performance**: in the [Telegram Bot API](https://core.telegram.org/bots/api), each `TDLib` instance handles more than 18000 active bots simultaneously.
* **Well-documented**: all `TDLib` API methods and public interfaces are fully documented.
* **Consistent**: `TDLib` guarantees that all updates are delivered in the right order.
* **Reliable**: `TDLib` remains stable on slow and unstable Internet connections.
* **Secure**: all local data is encrypted using a user-provided encryption key.
* **Fully-asynchronous**: requests to `TDLib` don't block each other or anything else, responses are sent when they are available.

<a name="usage"></a>
## Examples and documentation
Take a look at our [examples](https://github.com/tdlib/td/tree/master/example) and [documentation](https://core.telegram.org/tdlib/docs/).

<a name="dependencies"></a>
## Dependencies
`TDLib` depends on:

* C++14 compatible compiler (Clang 3.4+, GCC 4.9+, MSVC 19.0+ (Visual Studio 2015+), Intel C++ Compiler 17+)
* OpenSSL
* zlib
* gperf (build only)
* CMake (3.0.2+, build only)
* PHP (optional, for docs generation)
* Doxygen (optional, for docs generation)

<a name="building"></a>
## Building

Install all `TDLib` dependencies as described in [Installing dependencies](#installing-dependencies).
Then enter directory containing `TDLib` sources and compile them using CMake:

```
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

<a name="installing-dependencies"></a>
### Installing dependencies

#### OS X
* Install the latest XCode command line tools.
* Install other dependencies, for example, using [Homebrew](https://brew.sh):
```
brew install gperf cmake openssl
```
* Build `TDLib` with CMake as explained in [building](#building). You may need to manually specify path to the installed OpenSSL to CMake, e.g.,
```
cmake -DCMAKE_BUILD_TYPE=Release -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl/ ..
```

#### Windows
* Download and install [gperf](https://sourceforge.net/projects/gnuwin32/files/gperf/3.0.1/). Add the path to gperf to the PATH environment variable.
* Install [vcpkg](https://github.com/Microsoft/vcpkg#quick-start).
* Run the following commands:
```
C:\src\vcpkg> .\vcpkg install openssl zlib
```
* Build `TDLib` with CMake as explained in [building](#building), but instead of `cmake -DCMAKE_BUILD_TYPE=Release ..` use
  ```
  cmake -DCMAKE_TOOLCHAIN_FILE=C:\src\vcpkg\scripts\buildsystems\vcpkg.cmake ..
  ```

#### Linux
* Install all dependencies using your package manager.

<a name="using-cxx"></a>
## Using in CMake C++ projects
For C++ projects that use CMake, the best approach is to build `TDLib` as part of your project or to use a prebuilt installation.

There are several libraries that you could use in your CMake project:

* Td::TdJson, Td::TdJsonStatic — dynamic and static version of a json interface. Has a simple C interface, so it can be easily used with any programming language that supports C bindings.
* Td::TdStatic — static library with C++ interface.
* Td::TdCoreStatic — static library with low-level C++ interface intended mostly for internal usage.

For example, part of your CMakeLists.txt may look like this:
```
add_subdirectory(td)
target_link_libraries(YourTarget PRIVATE Td::TdStatic)
```

Or you could install `TDLib` and then reference it in your CMakeLists.txt like this:
```
find_package(Td 1.1.0 REQUIRED)
target_link_libraries(YourTarget PRIVATE Td::TdStatic)
```
See [example/cpp/CMakeLists.txt](https://github.com/tdlib/td/tree/master/example/cpp/CMakeLists.txt).

<a name="using-java"></a>
## Using in Java projects
TDLib provides native Java interface through JNI.

See [example/java](https://github.com/tdlib/td/tree/master/example/java) for example of using TDLib from Java and detailed build and usage instructions.

<a name="using-json"></a>
## Using from other programming languages
`TDLib` provides efficient native C++, Java, and C# (will be released soon) interfaces.
But for most use cases we suggest to use the JSON interface. It can be easily used with any language that supports C bindinds. See
[example/python/tdjson_example.py](https://github.com/tdlib/td/tree/master/example/python/tdjson_example.py) for an
example of such usage.

<a name="license"></a>
## License
The TDLib is licensed under the terms of the Boost Software License. See [LICENSE_1_0.txt](http://www.boost.org/LICENSE_1_0.txt) for more information.
