#!/bin/sh

DEST=tdweb/src/prebuilt/release/
mkdir -p $DEST
cp build/wasm/td_wasm.{js,wasm} $DEST
cp build/asmjs/td_asmjs.js{,.mem} $DEST
