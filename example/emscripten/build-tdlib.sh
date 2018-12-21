mkdir -p build/generate
mkdir -p build/asmjs
mkdir -p build/wasm

TD_ROOT=$(realpath ../../)
#OPENSSL_OPTIONS="-DOPENSSL_ROOT=$TD_ROOT/third_party/crypto/emscripten"
OPENSSL_ROOT=$(realpath ./build/crypto/)
OPENSSL_CRYPTO_LIBRARY=$OPENSSL_ROOT/lib/libcrypto.a
OPENSSL_SSL_LIBRARY=$OPENSSL_ROOT/lib/libssl.a

OPENSSL_OPTIONS="-DOPENSSL_FOUND=1 \
  -DOPENSSL_ROOT_DIR=\"$OPENSSL_ROOT\" \
  -DOPENSSL_INCLUDE_DIR=\"$OPENSSL_ROOT/include\" \
  -DOPENSSL_CRYPTO_LIBRARY=\"$OPENSSL_CRYPTO_LIBRARY\" \
  -DOPENSSL_SSL_LIBRARY=\"$OPENSSL_SSL_LIBRARY\" \
  -DOPENSSL_LIBRARIES=\"$OPENSSL_SSL_LIBRARY;$OPENSSL_CRYPTO_LIBRARY\" \
  -DOPENSSL_VERSION=\"1.1.0j\""

pushd .
cd build/wasm
eval emconfigure cmake $TD_ROOT -GNinja $OPENSSL_OPTIONS
popd

pushd .
cd build/asmjs
eval emconfigure cmake $TD_ROOT -GNinja -DASMJS=1 $OPENSSL_OPTIONS
popd

pushd .
cd build/generate
cmake $TD_ROOT -GNinja
popd

cmake --build build/generate -j --target prepare_cross_compiling 
cmake --build build/wasm -j --target td_wasm 
cmake --build build/asmjs -j --target td_asmjs 

