[CmdletBinding()]
param([string]$NdkDirectory, [string]$OpenXrDirectory, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$csRepo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$steamlinkBuildRoot = [IO.Path]::GetFullPath((Join-Path $csRepo '../builds/steamlink-patches'))
if (!$NdkDirectory) { $NdkDirectory = Join-Path $csRepo '.android-sdk/ndk/27.2.12479018' }
if (!$OpenXrDirectory) { $OpenXrDirectory = Join-Path $steamlinkBuildRoot 'extensions/controller-velocity-layer/build-android/_deps/openxr_headers-src' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $steamlinkBuildRoot 'build/codecs-support-work/native-direct' }
$csBin = Join-Path $NdkDirectory 'toolchains/llvm/prebuilt/windows-x86_64/bin'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$csLibrary = Join-Path $OutputDirectory 'libgxr_codecs_support.so'
& (Join-Path $csBin 'aarch64-linux-android29-clang++.cmd') -std=c++17 -O2 -fPIC -shared -fvisibility=hidden -static-libstdc++ -Wall -Wextra -Werror -Wno-missing-field-initializers `
    -DXR_USE_PLATFORM_ANDROID "-I$OpenXrDirectory/include" `
    (Join-Path $PSScriptRoot 'src/codecs_support_layer.cpp') -o $csLibrary `
    '-Wl,-soname,libgxr_codecs_support.so' '-Wl,-z,max-page-size=16384' '-Wl,--exclude-libs,ALL' -llog -ldl
if ($LASTEXITCODE -ne 0) { throw 'Codecs support Android compilation failed' }
& (Join-Path $csBin 'llvm-strip.exe') --strip-unneeded $csLibrary
if ($LASTEXITCODE -ne 0) { throw 'Codecs support strip failed' }
Get-FileHash -LiteralPath $csLibrary -Algorithm SHA256
Write-Host 'Build output only. Stage the validated helper and update its Kotlin SHA pin before packaging.'
