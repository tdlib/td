# TDLib Web example

This is an example of building `TDLib` for browsers using [Emscripten](https://github.com/kripken/emscripten).
These scripts build `TDLib` and creates an [NPM](https://www.npmjs.com/) package [tdweb](https://www.npmjs.com/package/@arseny30/tdweb).
You need Unix shell with `sed`, `tar` and `wget` utilities to run provided scripts.

## Building tdweb NPM package

* Install latest [emsdk](https://kripken.github.io/emscripten-site/docs/getting_started/downloads.html). Do not use system-provided `emscripten` package, because it contains too old version.
* Install all `TDLib` build dependencies as described in [Building](https://github.com/tdlib/td#building).
* Run `source ./emsdk_env.sh` from `emsdk` directory to set up correct build environment.
* On `macOS` install `coreutils` [Homebrew](https://brew.sh) package and replace `realpath` usages in scripts with `grealpath`:
```
brew install coreutils
sed -i.bak 's/[(]realpath/(grealpath/g' build-tdlib.sh
```
* Run build scripts in the following order:
```
cd <path to TDLib sources>/example/web
./build-openssl.sh
./build-tdlib.sh
./copy-tdlib.sh
./build-tdweb.sh
```
* The built package is now located in `tdweb` directory.

## Using tdweb NPM package

See [tdweb](https://www.npmjs.com/package/@arseny30/tdweb) or [README.md](https://github.com/tdlib/td/tree/master/example/web/tdweb/README.md) for the package documentation.
