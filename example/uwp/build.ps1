param (
  [string]$vcpkg_root = $(throw "-vcpkg_root=<path to vcpkg> is required"),
  [string]$arch = "",
  [string]$mode = "all",
  [string]$compress = "7z"
)
$ErrorActionPreference = "Stop"

$vcpkg_root = Resolve-Path $vcpkg_root

$vcpkg_cmake="${vcpkg_root}\scripts\buildsystems\vcpkg.cmake"
$arch_list = @( "x86", "x64", "ARM" )
if ($arch) {
  $arch_list = @(, $arch)
}

$td_root = Resolve-Path "../.."

function CheckLastExitCode {
  if ($LastExitCode -ne 0) {
    $msg = @"
EXE RETURNED EXIT CODE $LastExitCode
CALLSTACK:$(Get-PSCallStack | Out-String)
"@
    throw $msg
  }
}

function clean {
  Remove-Item build-* -Force -Recurse -ErrorAction SilentlyContinue
}

function prepare {
  New-Item -ItemType Directory -Force -Path build-native

  cd build-native

  cmake "$td_root" -A Win32 -DCMAKE_TOOLCHAIN_FILE="$vcpkg_cmake" -DTD_ENABLE_DOTNET=ON
  CheckLastExitCode
  cmake --build . --target prepare_cross_compiling
  CheckLastExitCode

  cd ..
}

function config {
  New-Item -ItemType Directory -Force -Path build-uwp
  cd build-uwp

  ForEach($arch in $arch_list) {
    echo "Config Arch = [$arch]"
    New-Item -ItemType Directory -Force -Path $arch
    cd $arch
    echo "${td_root}"
    $fixed_arch = $arch
    if ($arch -eq "x86") {
      $fixed_arch = "win32"
    }
    cmake "$td_root" -A $fixed_arch -DCMAKE_SYSTEM_VERSION="10.0" -DCMAKE_SYSTEM_NAME="WindowsStore" -DCMAKE_TOOLCHAIN_FILE="$vcpkg_cmake" -DTD_ENABLE_DOTNET=ON
    CheckLastExitCode
    cd ..
  }
  echo "done"
  cd ..
}

function build {
  cd build-uwp
  ForEach($arch in $arch_list) {
    echo "Build Arch = [$arch]"
    cd $arch
    cmake --build . --config Release --target tddotnet
    cmake --build . --config Debug --target tddotnet
    cd ..
  }
  cd ..
}

function export {
  cd build-uwp
  Remove-Item vsix -Force -Recurse -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force -Path vsix
  cp ../SDKManifest.xml vsix
  cp ../extension.vsixmanifest vsix
  cp '../`[Content_Types`].xml' vsix
  cp ../LICENSE_1_0.txt vsix

  ForEach($arch in $arch_list) {
    New-Item -ItemType Directory -Force -Path vsix/DesignTime/Debug/${arch}
    New-Item -ItemType Directory -Force -Path vsix/DesignTime/Retail/${arch}
    New-Item -ItemType Directory -Force -Path vsix/Redist/Debug/${arch}
    New-Item -ItemType Directory -Force -Path vsix/Redist/Retail/${arch}
    New-Item -ItemType Directory -Force -Path vsix/References/CommonConfiguration/${arch}

    cp ${arch}/Debug/* -include "SSLEAY*","LIBEAY*","libcrypto*","libssl*","zlib*" vsix/Redist/Debug/${arch}/
    cp ${arch}/Release/* -include "SSLEAY*","LIBEAY*","libcrypto*","libssl*","zlib*" vsix/Redist/Retail/${arch}/

    cp ${arch}/Debug/* -filter "Telegram.Td.*" -include "*.lib" vsix/DesignTime/Debug/${arch}/
    cp ${arch}/Release/*  -filter "Telegram.Td.*" -include "*.lib" vsix/DesignTime/Retail/${arch}/

    cp ${arch}/Debug/*  -filter "Telegram.Td.*" -include "*.pdb","*.dll" vsix/Redist/Debug/${arch}/
    cp ${arch}/Release/*  -filter "Telegram.Td.*" -include "*.pdb","*.dll" vsix/Redist/Retail/${arch}/

    cp ${arch}/Release/* -filter "Telegram.Td.*" -include "*.pri","*.winmd","*.xml" vsix/References/CommonConfiguration/${arch}/
  }

  cd vsix

  if ($compress -eq "zip") {
    zip -r tdlib.vsix *
  } elseif ($compress -eq "winrar") {
    WinRAR.exe a -afzip -r -ep1 tdlib.vsix *
  } else {
    7z.exe a -tzip -r tdlib.vsix *
  }
  cd ..
}

function run {
  Push-Location
  Try {
    if ($mode -eq "clean") {
      clean
    }
    if (($mode -eq "prepare") -or ($mode -eq "all")) {
      prepare
    }
    if (($mode -eq "config") -or ( $mode -eq "all")) {
      config
    }
    if (($mode -eq "build") -or ($mode -eq "all")) {
      build
    }
    if (($mode -eq "export") -or ($mode -eq "all")) {
      export
    }
  } Finally {
    Pop-Location
  }
}

run
