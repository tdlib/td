#!/bin/sh
td_path=$(grealpath ../..)

rm -rf build
mkdir -p build
cd build

set_cmake_options () {
  # Set CMAKE options depending on platform passed $1
  openssl_path=$(grealpath ../third_party/openssl/$1)
  echo "OpenSSL path = ${openssl_path}"
  openssl_crypto_library="${openssl_path}/lib/libcrypto.a"
  openssl_ssl_library="${openssl_path}/lib/libssl.a"
  options=""
  options="$options -DOPENSSL_FOUND=1"
  options="$options -DOPENSSL_CRYPTO_LIBRARY=${openssl_crypto_library}"
  options="$options -DOPENSSL_SSL_LIBRARY=${openssl_ssl_library}"
  options="$options -DOPENSSL_INCLUDE_DIR=${openssl_path}/include"
  options="$options -DOPENSSL_LIBRARIES=${openssl_crypto_library};${openssl_ssl_library}"
  options="$options -DCMAKE_BUILD_TYPE=Release"
}

platforms="macOS iOS watchOS tvOS"
#platforms="watchOS"
for platform in $platforms;
do
  echo "Platform = ${platform}"
  if [[ $platform = "macOS" ]]; then
    set_cmake_options $platform
    build="build-${platform}"
    install="install-${platform}"
    rm -rf $build
    mkdir -p $build
    mkdir -p $install
    cd $build
    cmake $td_path $options -DCMAKE_INSTALL_PREFIX=../${install}
    make -j3 install || exit
    cd ..
    mkdir -p $platform
    cp $build/libtdjson.dylib $platform/libtdjson.dylib
    install_name_tool -id @rpath/libtdjson.dylib $platform/libtdjson.dylib
  else
    simulators="0 1"
    for simulator in $simulators;
    do
      build="build-${platform}"
      install="install-${platform}"
      if [[ $simulator = "1" ]]; then
        build="${build}-simulator"
        install="${install}-simulator"
        ios_platform="SIMULATOR"
        set_cmake_options ${platform}-simulator
      else
        ios_platform="OS"
        set_cmake_options ${platform}
      fi
      watchos=""
      if [[ $platform = "watchOS" ]]; then
        ios_platform="WATCH${ios_platform}"
        watchos="-DTD_EXPERIMENTAL_WATCH_OS=ON"
      fi
      if [[ $platform = "tvOS" ]]; then
        ios_platform="TV${ios_platform}"
      fi
      echo $ios_platform
      rm -rf $build
      mkdir -p $build
      mkdir -p $install
      cd $build
      cmake $td_path $options $watchos -DIOS_PLATFORM=${ios_platform} -DCMAKE_TOOLCHAIN_FILE=${td_path}/CMake/iOS.cmake -DCMAKE_INSTALL_PREFIX=../${install}
      make -j3 install || exit
      cd ..
    done
    lib="install-${platform}/lib/libtdjson.dylib"
    lib_simulator="install-${platform}-simulator/lib/libtdjson.dylib"
    mkdir -p $platform
    install_name_tool -id @rpath/libtdjson.dylib $lib
    install_name_tool -id @rpath/libtdjson.dylib $lib_simulator
  fi

  echo "TODO: Make xcframework from build/install-*/lib/libtdjson.dylib"
done
