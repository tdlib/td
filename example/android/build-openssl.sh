#!/usr/bin/env bash
cd $(dirname $0)

ANDROID_SDK_ROOT=${1:-SDK}
ANDROID_NDK_VERSION=${2:-23.2.8568313}
OPENSSL=${3:-OpenSSL_1_1_1q} # openssl-3.0.5

OPENSSL_INSTALL_DIR="third-party/openssl"
if [ -d "$OPENSSL_INSTALL_DIR" ] ; then
  echo "Error: directory $OPENSSL_INSTALL_DIR already exists. Delete it manually to proceed."
  exit 1
fi

source ./check-environment.sh

ANDROID_SDK_ROOT="$(cd "$(dirname -- "$ANDROID_SDK_ROOT")" >/dev/null; pwd -P)/$(basename -- "$ANDROID_SDK_ROOT")"

if [[ "$OS_NAME" == "linux" ]] ; then
  HOST_ARCH="linux-x86_64"
elif [[ "$OS_NAME" == "mac" ]] ; then
  HOST_ARCH="darwin-x86_64"
elif [[ "$OS_NAME" == "win" ]] ; then
  HOST_ARCH="windows-x86_64"
else
  echo "Error: unsupported OS_NAME."
  exit 1
fi

if [ ! -f $OPENSSL.tar.gz ] ; then
  echo "Downloading OpenSSL sources..."
  $WGET https://github.com/openssl/openssl/archive/refs/tags/$OPENSSL.tar.gz || exit 1
fi
rm -rf ./openssl-$OPENSSL || exit 1
tar xzf $OPENSSL.tar.gz || exit 1
rm $OPENSSL.tar.gz || exit 1
cd openssl-$OPENSSL

export ANDROID_NDK_ROOT=$ANDROID_SDK_ROOT/ndk/$ANDROID_NDK_VERSION  # for OpenSSL 3.0
export ANDROID_NDK_HOME=$ANDROID_NDK_ROOT                           # for OpenSSL 1.1.1
PATH=$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/$HOST_ARCH/bin:$PATH

for ARCH in arm64 arm x86_64 x86 ; do
  if [[ $ARCH == "x86" ]]; then
    ./Configure android-x86 no-shared -U__ANDROID_API__ -D__ANDROID_API__=16 || exit 1
  elif [[ $ARCH == "x86_64" ]]; then
    ./Configure android-x86_64 no-shared -U__ANDROID_API__ -D__ANDROID_API__=21 || exit 1
  elif [[ $ARCH == "arm" ]]; then
    ./Configure android-arm no-shared -U__ANDROID_API__ -D__ANDROID_API__=16 -D__ARM_MAX_ARCH__=8 || exit 1
  elif [[ $ARCH == "arm64" ]]; then
    ./Configure android-arm64 no-shared -U__ANDROID_API__ -D__ANDROID_API__=21 || exit 1
  fi

  sed -i.bak 's/-O3/-O3 -ffunction-sections -fdata-sections/g' Makefile || exit 1

  make depend -s || exit 1
  make -j4 -s || exit 1

  rm -rf ../$OPENSSL_INSTALL_DIR/$ARCH/* || exit 1
  mkdir -p ../$OPENSSL_INSTALL_DIR/$ARCH/lib/ || exit 1
  cp libcrypto.a libssl.a ../$OPENSSL_INSTALL_DIR/$ARCH/lib/ || exit 1
  cp -r include ../$OPENSSL_INSTALL_DIR/$ARCH/ || exit 1

  make distclean || exit 1
done

cd ..

rm -rf ./openssl-$OPENSSL || exit 1
