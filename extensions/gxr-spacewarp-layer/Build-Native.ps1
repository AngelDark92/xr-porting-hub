[CmdletBinding()]
param([string]$NdkDirectory, [string]$OpenXrDirectory, [string]$OutputDirectory)
# gxr-spacewarp extension build (09-LAYER-B1b component 5), adapted from
# codecs-support-layer/Build-Native.ps1. Output only: staging the validated
# .so into patches/src/main/resources/steamlink/androidxr/ and updating the
# Kotlin SHA pins is a separate, owner-owned step ("Build output only" below).
$ErrorActionPreference = 'Stop'
$swRepo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$steamlinkBuildRoot = [IO.Path]::GetFullPath((Join-Path $swRepo '../builds/steamlink-patches'))
if (!$NdkDirectory) { $NdkDirectory = Join-Path $swRepo '.android-sdk/ndk/27.2.12479018' }
if (!$OpenXrDirectory) {
    # Reuse the OpenXR headers the other layers' build trees already downloaded
    # (shared-read; never delete or move them).
    $OpenXrDirectory = Join-Path $steamlinkBuildRoot 'extensions/controller-velocity-layer/build-android/_deps/openxr_headers-src'
}
if (!$OutputDirectory) { $OutputDirectory = Join-Path $steamlinkBuildRoot 'build/gxr-spacewarp-work/native-direct' }
$swBin = Join-Path $NdkDirectory 'toolchains/llvm/prebuilt/windows-x86_64/bin'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$swLibrary = Join-Path $OutputDirectory 'libgxr_spacewarp.so'

# Estimator core: the sibling builder (brief 08) owns spacewarp_est.h/.cpp,
# swp_shaders.h, swp_est_worker.cpp. When present, compile/link them; when
# absent, swp_dispatch.cpp falls back to fail-safe stand-down stubs so the
# skeleton links standalone (guard verified against `__has_include`).
$sources = @(
    (Join-Path $PSScriptRoot 'src/swp_dispatch.cpp'),
    (Join-Path $PSScriptRoot 'src/swp_hooks.cpp'),
    (Join-Path $PSScriptRoot 'src/swp_egl.cpp'),
    (Join-Path $PSScriptRoot 'src/swp_vts.cpp'),
    (Join-Path $PSScriptRoot 'src/xr_layer_entry.cpp')
)
$estimator = Join-Path $PSScriptRoot 'src/spacewarp_est.cpp'
$worker = Join-Path $PSScriptRoot 'src/swp_est_worker.cpp'
$header = Join-Path $PSScriptRoot 'src/spacewarp_est.h'
if ((Test-Path $worker -PathType Leaf) -and !(Test-Path $header -PathType Leaf)) {
    throw 'swp_est_worker.cpp present but spacewarp_est.h missing (sibling builder files)'
}
if ((Test-Path $estimator -PathType Leaf) -and !(Test-Path $header -PathType Leaf)) {
    # spacewarp_est.cpp without its header would double-define the contract
    # symbols against the dispatcher's stand-down stubs; refuse the mixed state.
    throw 'spacewarp_est.cpp present but spacewarp_est.h missing (sibling builder files)'
}
if (Test-Path $estimator -PathType Leaf) { $sources += $estimator }
if (Test-Path $worker -PathType Leaf) { $sources += $worker }

& (Join-Path $swBin 'aarch64-linux-android29-clang++.cmd') -std=c++17 -O2 -fPIC -shared -fvisibility=hidden -static-libstdc++ -Wall -Wextra -Werror -Wno-missing-field-initializers `
    -DXR_USE_PLATFORM_ANDROID -DXR_USE_GRAPHICS_API_OPENGL_ES "-I$OpenXrDirectory/include" `
    $sources -o $swLibrary `
    '-Wl,-soname,libgxr_spacewarp.so' '-Wl,-z,max-page-size=16384' '-Wl,--exclude-libs,ALL' -llog -lEGL -lGLESv3
if ($LASTEXITCODE -ne 0) { throw 'GXR spacewarp Android compilation failed' }
& (Join-Path $swBin 'llvm-strip.exe') --strip-unneeded $swLibrary
if ($LASTEXITCODE -ne 0) { throw 'GXR spacewarp strip failed' }
Get-FileHash -LiteralPath $swLibrary -Algorithm SHA256
Write-Host 'Build output only. Stage the validated .so into patches/src/main/resources/steamlink/androidxr/ and update the Kotlin SHA pin before packaging.'