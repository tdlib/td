dest=tdweb/src/prebuilt/release/
mkdir -p $dest
cp build/wasm/td_wasm.{js,wasm} $dest
cp build/asmjs/td_asmjs.js{,.mem} $dest
