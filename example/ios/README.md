# Build for iOS

Bellow are instructions for building TdLib for iOS, watchOS and also macOS.

If you need just macOS build take a look [here](https://github.com/tdlib/td#os-x).

For example of usage take a look [here](https://github.com/tdlib/td/tree/master/example/swift).

## Build OpenSSL
First, you should build OpenSSL library for ios
```
./build-openssl.sh
```
In this example we are using scripts from [Python Apple support](https://github.com/pybee/Python-Apple-support), but any other OpenSSL builds should work too.
Libraries will be stored in `third_party/openssl/<platfrom>`. The next script will rely on this location.

## Build TdLib
Run:
```
./build.sh
```
This may take a while, because TdLib will be build about 8 times.
As an upside resulting library for iOS will work on any architecture (arv7, armv7s, arm64) and even on a simulator.
We use  [CMake/iOS.cmake](https://github.com/tdlib/td/blob/master/CMake/iOS.cmake) toolchain, other toolchains
may work too. 

Libraries will be store in `tdjson` directory.


