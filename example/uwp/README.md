# TDLib Universal Windows Platform example

Here are script for building TDLib SDK for universal windows platfrom and an example of its usage.

## Building SDK

1. Install [vcpkg](https://github.com/Microsoft/vcpkg).

2. With `vcpkg` install `zlib` and `openssl` for all UWP architectures.
```
.\vcpkg.exe install openssl:arm-uwp openssl:arm64-uwp openssl:x64-uwp openssl:x86-uwp zlib:arm-uwp zlib:arm64-uwp zlib:x64-uwp zlib:x86-uwp
```
3. Install 7z. You may also use zip or winrar.

4. Run build.ps1 with `powershell`.
Pass path to vcpkg as `-vcpkg-root` argument.
```
.\build.ps1 -vcpkg-root "~\vcpkg\"
```

If you wish to use `zip` or `WinRAR` instead of `7z` pass `-compress zip` or `-compress winrar`.

The build process will take a lot of time, as TDLib will be built for multiple
platforms in Debug and Release configurations. You may interrupt and resume the script at any moment.

If you will need to restart the build from scratch, call `.\build.ps -mode "clean"` first.

5. `tdlib.vsix` will be at `build-uwp\vsix\tdlib.vsix`. Open it and install the SDK.

## Example of usage
`app` directory contains a simple example of app for Universal Windows Plaform. Just open it with Visual Studio 2015 or 2017 and run.


