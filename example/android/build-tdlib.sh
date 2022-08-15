#!/usr/bin/env bash

ANDROID_SDK_ROOT=${1:-SDK}
ANDROID_NDK_VERSION=${2:-23.2.8568313}
OPENSSL_INSTALL_DIR=${3:-third-party/openssl}
ANDROID_STL=${4:-c++_static}

if [ "$ANDROID_STL" != "c++_static" ] && [ "$ANDROID_STL" != "c++_shared" ] ; then
  echo 'Error: ANDROID_STL must be either "c++_static" or "c++_shared".'
  exit 1
fi

source ./check-environment.sh || exit 1

if [ ! -d "$ANDROID_SDK_ROOT" ] ; then
  echo "Error: directory \"$ANDROID_SDK_ROOT\" doesn't exist. Run ./fetch-sdk.sh first, or provide a valid path to Android SDK."
  exit 1
fi

if [ ! -d "$OPENSSL_INSTALL_DIR" ] ; then
  echo "Error: directory \"$OPENSSL_INSTALL_DIR\" doesn't exists. Run ./build-openssl.sh first."
  exit 1
fi

ANDROID_SDK_ROOT="$(cd "$(dirname -- "$ANDROID_SDK_ROOT")" >/dev/null; pwd -P)/$(basename -- "$ANDROID_SDK_ROOT")"
ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/$ANDROID_NDK_VERSION"
OPENSSL_INSTALL_DIR="$(cd "$(dirname -- "$OPENSSL_INSTALL_DIR")" >/dev/null; pwd -P)/$(basename -- "$OPENSSL_INSTALL_DIR")"
PATH=$ANDROID_SDK_ROOT/cmake/3.22.1/bin:$PATH

cd $(dirname $0)

echo "Downloading annotation Java package..."
rm -f android.jar annotation-1.4.0.jar || exit 1
$WGET https://maven.google.com/androidx/annotation/annotation/1.4.0/annotation-1.4.0.jar || exit 1

echo "Generating TDLib source files..."
mkdir -p build-native || exit 1
cd build-native
cmake .. || exit 1
cmake --build . --target prepare_cross_compiling || exit 1
cmake --build . --target tl_generate_java || exit 1
cd ..
php AddIntDef.php org/drinkless/tdlib/TdApi.java || exit 1

echo "Copying Java source files..."
rm -rf tdlib || exit 1
mkdir -p tdlib/java/org/drinkless/tdlib || exit 1
cp -p {../../example,tdlib}/java/org/drinkless/tdlib/Client.java || exit 1
mv {,tdlib/java/}org/drinkless/tdlib/TdApi.java || exit 1
rm -rf org || exit 1

echo "Generating Javadoc documentation..."
cp "$ANDROID_SDK_ROOT/platforms/android-33/android.jar" . || exit 1
JAVADOC_SEPARATOR=$([ "$OS_NAME" == "win" ] && echo ";" || echo ":")
javadoc -d tdlib/javadoc -encoding UTF-8 -charset UTF-8 -classpath "android.jar${JAVADOC_SEPARATOR}annotation-1.4.0.jar" -quiet -sourcepath tdlib/java org.drinkless.tdlib || exit 1
rm android.jar annotation-1.4.0.jar || exit 1

echo "Building TDLib..."
for ABI in arm64-v8a armeabi-v7a x86_64 x86 ; do
  mkdir -p build-$ABI || exit 1
  cd build-$ABI
  cmake -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" -DOPENSSL_ROOT_DIR="$OPENSSL_INSTALL_DIR/$ABI" -DCMAKE_BUILD_TYPE=RelWithDebInfo -GNinja -DANDROID_ABI=$ABI -DANDROID_STL=$ANDROID_STL -DANDROID_PLATFORM=android-16 .. || exit 1
  cmake --build . || exit 1
  cd ..

  mkdir -p tdlib/libs/$ABI/ || exit 1
  cp -p build-$ABI/libtd*.so* tdlib/libs/$ABI/ || exit 1
  if [[ "$ANDROID_STL" == "c++_shared" ]] ; then
    if [[ "$ABI" == "arm64-v8a" ]] ; then
      FULL_ABI="aarch64-linux-android"
    elif [[ "$ABI" == "armeabi-v7a" ]] ; then
      FULL_ABI="arm-linux-androideabi"
    elif [[ "$ABI" == "x86_64" ]] ; then
      FULL_ABI="x86_64-linux-android"
    elif [[ "$ABI" == "x86" ]] ; then
      FULL_ABI="i686-linux-android"
    fi
    cp "$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/$HOST_ARCH/sysroot/usr/lib/$FULL_ABI/libc++_shared.so" tdlib/libs/$ABI/ || exit 1
    "$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/$HOST_ARCH/bin/llvm-strip" tdlib/libs/$ABI/libc++_shared.so || exit 1
  fi
done

echo "Compressing..."
rm -f tdlib.zip tdlib-debug.zip || exit 1
jar -cMf tdlib-debug.zip tdlib || exit 1
rm tdlib/libs/*/*.debug || exit 1
jar -cMf tdlib.zip tdlib || exit 1
mv tdlib.zip tdlib-debug.zip tdlib || exit 1

echo "Done."
