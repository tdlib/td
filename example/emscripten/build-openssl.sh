#!/bin/sh

OPENSSL=OpenSSL_1_1_0j
if [ ! -f $OPENSSL.tar.gz ]; then
  echo "Download openssl"
  wget https://github.com/openssl/openssl/archive/$OPENSSL.tar.gz
fi
rm -rf ./$OPENSSL
tar xzf $OPENSSL.tar.gz || exit 1
cd openssl-$OPENSSL

emconfigure ./Configure linux-generic32 no-shared
sed -i bak 's/CROSS_COMPILE=.*/CROSS_COMPILE=/g' Makefile
emmake make depend -s || exit 1
emmake make -s || exit 1

rm -rf ../build/crypto || exit 1
mkdir -p ../build/crypto/lib || exit 1
cp libcrypto.a libssl.a ../build/crypto/lib/ || exit 1
cp -r include ../build/crypto/ || exit 1
cd ..
