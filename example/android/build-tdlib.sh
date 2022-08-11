#!/usr/bin/env bash
cd $(dirname $0)

ANDROID_SDK_ROOT=${1:-SDK}
ANDROID_NDK_VERSION=${2:-23.2.8568313}

source ./check-environment.sh

echo "Downloading annotation Java package..."
rm -rf annotation || exit 1
mkdir -p annotation || exit 1
cd annotation
$WGET https://maven.google.com/androidx/annotation/annotation/1.4.0/annotation-1.4.0.pom || exit 1
$WGET https://maven.google.com/androidx/annotation/annotation/1.4.0/annotation-1.4.0.jar || exit 1
cd ..

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
javadoc -d tdlib/javadoc -encoding UTF-8 -charset UTF-8 -bootclasspath $ANDROID_SDK_ROOT/platforms/android-33/android.jar -extdirs annotation -classpath tdlib/java org.drinkless.tdlib || exit 1
rm -rf annotation || exit 1

ANDROID_SDK_ROOT="$(cd "$(dirname -- "$ANDROID_SDK_ROOT")" >/dev/null; pwd -P)/$(basename -- "$ANDROID_SDK_ROOT")"

echo "Building TDLib..."
for ABI in arm64-v8a armeabi-v7a x86_64 x86 ; do
  mkdir -p build-$ABI || exit 1
  cd build-$ABI
  cmake -DCMAKE_TOOLCHAIN_FILE=${ANDROID_SDK_ROOT}/ndk/${ANDROID_NDK_VERSION}/build/cmake/android.toolchain.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -GNinja -DANDROID_ABI=${ABI} -DANDROID_PLATFORM=android-16 .. || exit 1
  cmake --build . || exit 1
  cd ..

  mkdir -p tdlib/libs/$ABI/ || exit 1
  cp -p build-$ABI/libtd*.so* tdlib/libs/$ABI/ || exit 1
done

echo "Compressing..."
jar -cMf tdlib-debug.zip tdlib || exit 1
rm tdlib/libs/*/*.debug || exit 1
jar -cMf tdlib.zip tdlib || exit 1
mv tdlib.zip tdlib-debug.zip tdlib

echo "Done."
