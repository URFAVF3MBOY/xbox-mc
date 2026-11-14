# builds mobileglues.dll for the xbox (uwp, x64) with clang-cl
# .\mobileglues\build.ps1 -AngleDir staging\ci\angle-x64-uwp
# source: MobileGlues at the commit below + my patches in mobileglues\patches

param(
    [Parameter(Mandatory = $true)] [string] $AngleDir,
    [string] $Config = "Release"
)

$ErrorActionPreference = "Stop"
$commit = "97558a69157b033b890958b1e49258c5b51a80c6"
$here = $PSScriptRoot
$root = Split-Path $here
$src = Join-Path $root "staging\mg"
$bld = Join-Path $root "staging\mg-build"
$AngleDir = (Resolve-Path $AngleDir).Path

# git (full path, powershell finds an empty C:\Windows\git first on my pc)
$git = (Get-Command git.exe -All | Where-Object { $_.Source -like "*.exe" } | Select-Object -First 1).Source
if (-not $git) { throw "git.exe not found" }

# source
if (-not (Test-Path (Join-Path $src ".git"))) {
    & $git clone -q https://github.com/MobileGL-Dev/MobileGlues.git $src
}
& $git -C $src fetch -q origin $commit
& $git -C $src checkout -q -f $commit
& $git -C $src clean -q -fdx MobileGlues-cpp
& $git -C $src submodule update -q --init --depth 1 MobileGlues-cpp/3rdparty/glslang MobileGlues-cpp/3rdparty/SPIRV-Cross MobileGlues-cpp/3rdparty/xxhash MobileGlues-cpp/include/ska
foreach ($p in Get-ChildItem (Join-Path $here "patches") -Filter *.patch | Sort-Object Name) {
    & $git -C $src apply --whitespace=nowarn $p.FullName
    if ($LASTEXITCODE -ne 0) { throw "patch $($p.Name) failed" }
}

# tools
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$env:PATH = (Split-Path $vswhere) + ";" + $env:PATH
$vsPath = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
$cmake = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$clang = Join-Path $vsPath "VC\Tools\Llvm\x64\bin\clang-cl.exe"
foreach ($t in @($cmake, $ninja, $clang)) { if (-not (Test-Path $t)) { throw "missing $t" } }

$compat = (Join-Path $here "compat").Replace("\", "/")
$angleInc = (Join-Path $AngleDir "include").Replace("\", "/")
# uwp: app api family, app crt from vcvars "store"
$flags = "/FI$compat/mg_win.h /DWINAPI_FAMILY=WINAPI_FAMILY_APP /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DWIN32_LEAN_AND_MEAN -Wno-everything"

New-Item -ItemType Directory -Force $bld | Out-Null
$bat = Join-Path $bld "build.bat"
@"
@echo off
call "$vcvars" store >nul || exit /b 1
"$cmake" -S "$src\MobileGlues-cpp" -B "$bld" -G Ninja -DCMAKE_MAKE_PROGRAM="$ninja" ^
  -DCMAKE_BUILD_TYPE=$Config -DCMAKE_C_COMPILER="$clang" -DCMAKE_CXX_COMPILER="$clang" ^
  -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags /EHsc" ^
  -DMG_COMPAT_DIR="$compat" -DMG_ANGLE_INCLUDE="$angleInc" ^
  -DCMAKE_EXE_LINKER_FLAGS="windowsapp.lib" -DCMAKE_SHARED_LINKER_FLAGS="/APPCONTAINER windowsapp.lib" || exit /b 1
"$cmake" --build "$bld" || exit /b 1
"@ | Set-Content -Encoding ascii $bat
cmd /c "`"$bat`""
if ($LASTEXITCODE -ne 0) { throw "mobileglues build failed" }
Get-ChildItem $bld -Filter mobileglues.dll | ForEach-Object { Write-Host "built $($_.FullName)" }
