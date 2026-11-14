# Build a static QuickJS-NG for the current platform into native/bin.
#
#   pwsh tools/build-quickjs.ps1
#   pwsh tools/build-quickjs.ps1 -Clean
#
# Windows: hxcpp compiles with -MT (static CRT), so QuickJS must use the same
# runtime or the two heaps fight. Hence CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded.

param(
    [switch]$Clean,
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$deps = Join-Path $root "native/quickjs"
$build = Join-Path $root "_build/qjs-native"

if (-not (Test-Path $deps)) {
    Write-Host "fetching quickjs-ng (pinned)..."
    git submodule update --init --recursive
}

if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }

$isWin = $IsWindows -or $env:OS -eq "Windows_NT"

if ($isWin) {
    cmake -S $deps -B $build -G "Visual Studio 17 2022" -A x64 -DBUILD_SHARED_LIBS=OFF -DQJS_BUILD_EXAMPLES=OFF -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
    cmake --build $build --config $Config --target qjs
    $out = Join-Path $root "native/bin/windows-x64"
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    Copy-Item (Join-Path $build "$Config/qjs.lib") $out -Force
    Write-Host "wrote $out\qjs.lib"
} else {
    cmake -S $deps -B $build -DCMAKE_BUILD_TYPE=$Config -DBUILD_SHARED_LIBS=OFF -DQJS_BUILD_EXAMPLES=OFF
    cmake --build $build --target qjs -j
    $plat = if ($IsMacOS) { "mac" } else { "linux-x64" }
    $out = Join-Path $root "native/bin/$plat"
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    Copy-Item (Join-Path $build "libqjs.a") $out -Force
    Write-Host "wrote $out/libqjs.a"
}