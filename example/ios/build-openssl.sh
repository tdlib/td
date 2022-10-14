#!/bin/sh
cd $(dirname $0)

git clone https://github.com/beeware/Python-Apple-support
cd Python-Apple-support
git checkout 60b990128d5f1f04c336ff66594574515ab56604
git apply ../Python-Apple-support.patch
cd ..

#TODO: change openssl version
platforms="macOS iOS watchOS tvOS"

for platform in $platforms;
do
  if [[ $platform = "macOS" ]]; then
    simulators="0"
  else
    simulators="0 1"
  fi

  for simulator in $simulators;
  do
    if [[ $simulator = "1" ]]; then
      platform="${platform}-simulator"
    fi
    echo $platform
    cd Python-Apple-support
    #NB: -j will fail
    make OpenSSL-$platform || exit 1
    cd ..
    rm -rf third_party/openssl/$platform || exit 1
    mkdir -p third_party/openssl/$platform/lib || exit 1
    cp ./Python-Apple-support/build/$platform/libcrypto.a third_party/openssl/$platform/lib/ || exit 1
    cp ./Python-Apple-support/build/$platform/libssl.a third_party/openssl/$platform/lib/ || exit 1
    cp -r ./Python-Apple-support/build/$platform/openssl/include/ third_party/openssl/$platform/include || exit 1
  done
done
