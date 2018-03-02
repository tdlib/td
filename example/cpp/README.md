# TDLib cpp basic usage examples

TDLib should be prebuilt and installed to local subdirectory `td/`:
```
cd <path to TDLib sources>
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX:PATH=../example/cpp/td ..
cmake --build . --target install
```
Also see [building](https://github.com/tdlib/td#building) for additional details on TDLib building.

Then you can build the examples:
```
cd <path to TDLib sources>/example/cpp
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DTd_DIR=<full path to TDLib sources>/example/cpp/td/lib/cmake/Td ..
cmake --build .
```
