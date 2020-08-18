# TDLib Java example

To run this example, you will need installed JDK >= 1.6.
For Javadoc documentation generation PHP is needed.

TDLib should be prebuilt with JNI bindings and installed to local subdirectory `td/` as follows:
```
cd <path to TDLib sources>
mkdir jnibuild
cd jnibuild
cmake -DCMAKE_BUILD_TYPE=Release -DTD_ENABLE_JNI=ON -DCMAKE_INSTALL_PREFIX:PATH=../example/java/td ..
cmake --build . --target install
```
If you want to compile TDLib for 32-bit/64-bit Java on Windows using MSVC, you will also need to add `-A Win32`/`-A x64` option to CMake.

In Windows, use Vcpkg toolchain file by adding parameter -DCMAKE_TOOLCHAIN_FILE=<VCPKG_DIR>/scripts/buildsystems/vcpkg.cmake

Then you can build this example:
```
cd <path to TDLib sources>/example/java
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DTd_DIR=<full path to TDLib sources>/example/java/td/lib/cmake/Td -DCMAKE_INSTALL_PREFIX:PATH=.. ..
cmake --build . --target install
```

Compiled TDLib shared library and Java example after that will be placed in bin/ and Javadoc documentation in `docs/`.

Now you can run Java example:
```
cd <path to TDLib sources>/example/java/bin
java '-Djava.library.path=.' org/drinkless/tdlib/example/Example
```

If you get "Could NOT find JNI ..." error from CMake, you need to specify to CMake path to the installed JDK, for example, "-DJAVA_HOME=/usr/lib/jvm/java-8-oracle/".

If you get java.lang.UnsatisfiedLinkError with "Can't find dependent libraries", you may also need to copy some dependent shared OpenSSL and zlib libraries to `bin/`.

In case you compiled the example as 32-bit version, you may need to give -d32 parameter to Java.
