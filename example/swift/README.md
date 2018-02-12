# TDLib swift MacOS example

TDLib should be prebuilt and installed to local subdirectory `td/`:
```
cd <path to TDLib sources>
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX:PATH=../example/swift/td ..
cmake --build . --target install
```

Then you can open and build the example with the latest Xcode.
