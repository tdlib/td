# Build for iOS

Below are instructions for building TDLib for iOS, watchOS, tvOS, and also macOS.

If you need only a macOS build, take a look at our build instructions for [macOS](https://github.com/tdlib/td#macos).

For example of usage take a look at our [Swift example](https://github.com/tdlib/td/tree/master/example/swift).

To compile `TDLib` you will need to:
* Install the latest Xcode via `xcode-select --install` or downloading it from [Xcode website](https://developer.apple.com/xcode/).
  It is not enough to install only command line developer tools to build `TDLib` for iOS.
* Install other build dependencies using [Homebrew](https://brew.sh):
```
brew install gperf cmake coreutils
```
* If you don't want to build `TDLib` for macOS, you can pregenerate required source code files using `CMake` prepare_cross_compiling target:
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
[Python Apple support](https://github.com/pybee/Python-Apple-support) has known problems with spaces in the path to the current directory, so
you need to ensure that there is no spaces in the path.
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
