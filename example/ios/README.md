# Build for iOS

Below are instructions for building TDLib for iOS, watchOS, tvOS, and also macOS.

If you need only a macOS build, take a look at our build instructions for [macOS](https://github.com/tdlib/td#macos).

For example of usage take a look at our [Swift example](https://github.com/tdlib/td/tree/master/example/swift).

To compile `TDLib` you will need to:
* Install the latest Xcode command line tools, for example, via `xcode-select --install`.
* Install other build dependencies, for example, using [Homebrew](https://brew.sh):
```
brew install gperf cmake
```
* If you don't want to build `TDLib` for macOS, you can pregenerate required source code files using CMake prepare_cross_compiling target:
```
cd <path to TDLib sources>
mkdir native-build
cd native-build
cmake ..
cmake --build . --target prepare_cross_compiling
```
* Build OpenSSL for iOS, watchOS, tvOS and macOS:
```
cd <path to TDLib sources>/example/ios
./build-openssl.sh
```
Here we use scripts from [Python Apple support](https://github.com/pybee/Python-Apple-support), but any other OpenSSL builds should work too.
Built libraries should be stored in `third_party/openssl/<platform>`, because the next script will rely on this location.
* Build TDLib for iOS, watchOS, tvOS and macOS:
```
cd <path to TDLib sources>/example/ios
./build.sh
```
This may take a while, because TDLib will be built about 10 times.
Resulting library for iOS will work on any architecture (armv7, armv7s, arm64) and even on a simulator.
We use [CMake/iOS.cmake](https://github.com/tdlib/td/blob/master/CMake/iOS.cmake) toolchain, other toolchains may work too.

Built libraries will be store in `tdjson` directory.

Documentation for all available classes and methods can be found at https://core.telegram.org/tdlib/docs.
