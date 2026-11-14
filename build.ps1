# builds the app and makes a signed appx
# .\build.ps1 -AngleDir staging\vcpkg_installed\x64-uwp
# no -PfxPath = makes a new self-signed cert, install the .cer on the xbox too

param(
    [Parameter(Mandatory = $true)] [string] $AngleDir,
    [string] $Version = "0.1.0.0",
    [string] $Publisher = "CN=xbox-mc-dev",
    [string] $PfxPath = "",
    [string] $PfxPassword = "",
    [string] $OutDir = "out"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$AngleDir = (Resolve-Path $AngleDir).Path
$stage = Join-Path $root "staging\package"
$obj = Join-Path $root "staging\obj"
$OutDir = Join-Path $root $OutDir
foreach ($d in @($stage, $obj, $OutDir)) {
    if (Test-Path $d) { Remove-Item -Recurse -Force $d }
    New-Item -ItemType Directory -Force $d | Out-Null
}

# tools
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "Visual Studio with the x64 C++ tools not found" }
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
# vcvars calls vswhere and complains when its not on PATH
$env:PATH = (Split-Path $vswhere) + ";" + $env:PATH

$kits = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10"
$sdkVer = (Get-ChildItem (Join-Path $kits "bin") -Directory |
    Where-Object { $_.Name -match '^10\.' -and (Test-Path (Join-Path $_.FullName "x64\makeappx.exe")) } |
    Sort-Object { [version]$_.Name } | Select-Object -Last 1).Name
if (-not $sdkVer) { throw "Windows SDK with makeappx.exe not found" }
$sdkBin = Join-Path $kits "bin\$sdkVer\x64"
Write-Host "Visual Studio: $vsPath"
Write-Host "Windows SDK:   $sdkVer"

# compile
# "store" = use the UWP CRT (comes from VCLibs)
$exe = Join-Path $stage "xbox-mc.exe"
$bat = Join-Path $obj "compile.bat"
@"
@echo off
call "$vcvars" store $sdkVer >nul || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MD /W3 /permissive- ^
  /DWINAPI_FAMILY=WINAPI_FAMILY_APP /DUNICODE /D_UNICODE /DNOMINMAX ^
  /I "$AngleDir\include" /Fo"$obj\\" ^
  "$root\host\main.cpp" ^
  /Fe"$exe" ^
  /link /APPCONTAINER /SUBSYSTEM:WINDOWS /LIBPATH:"$AngleDir\lib" libEGL.lib libGLESv2.lib windowsapp.lib
"@ | Set-Content -Encoding ascii $bat
cmd /c "`"$bat`""
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

# package folder
# all of angle's dlls (libEGL, libGLESv2, libz)
Copy-Item (Join-Path $AngleDir "bin\*.dll") $stage
# angle needs this to compile shaders
$d3dc = Join-Path $kits "Redist\D3D\x64\d3dcompiler_47.dll"
if (Test-Path $d3dc) { Copy-Item $d3dc $stage } else { Write-Warning "d3dcompiler_47.dll not found in the SDK redist" }

$manifest = Get-Content -Raw (Join-Path $root "host\Package.appxmanifest")
$manifest = $manifest.Replace("@PUBLISHER@", $Publisher).Replace("@VERSION@", $Version)
Set-Content -Encoding utf8 (Join-Path $stage "AppxManifest.xml") $manifest

# placeholder logos, manifest wont pack without them
Add-Type -AssemblyName System.Drawing
$assets = Join-Path $stage "Assets"
New-Item -ItemType Directory -Force $assets | Out-Null
$images = @{ "StoreLogo.png" = @(50, 50); "Square44x44Logo.png" = @(44, 44);
             "Square150x150Logo.png" = @(150, 150); "SplashScreen.png" = @(620, 300) }
foreach ($name in $images.Keys) {
    $w = $images[$name][0]; $h = $images[$name][1]
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $gfx.Clear([System.Drawing.Color]::FromArgb(27, 27, 27))
    $s = [Math]::Min($w, $h) * 0.6
    $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(98, 180, 122))
    $gfx.FillRectangle($brush, ($w - $s) / 2, ($h - $s) / 2, $s, $s)
    $gfx.Dispose()
    $bmp.Save((Join-Path $assets $name), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

# pack + sign
$appx = Join-Path $OutDir "xbox-mc_$Version`_x64.appx"
& (Join-Path $sdkBin "makeappx.exe") pack /o /d $stage /p $appx
if ($LASTEXITCODE -ne 0) { throw "makeappx failed" }

if (-not $PfxPath) {
    $certDir = Join-Path $root "staging\cert"
    New-Item -ItemType Directory -Force $certDir | Out-Null
    $PfxPath = Join-Path $certDir "xbox-mc-dev.pfx"
    $PfxPassword = [Guid]::NewGuid().ToString("N")
    $cert = New-SelfSignedCertificate -Type Custom -Subject $Publisher `
        -KeyUsage DigitalSignature -FriendlyName "xbox-mc dev signing" `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
    $sec = ConvertTo-SecureString $PfxPassword -AsPlainText -Force
    Export-PfxCertificate -Cert $cert -FilePath $PfxPath -Password $sec | Out-Null
    Export-Certificate -Cert $cert -FilePath (Join-Path $OutDir "xbox-mc-dev.cer") | Out-Null
    Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)"
}
& (Join-Path $sdkBin "signtool.exe") sign /q /fd SHA256 /f $PfxPath /p $PfxPassword $appx
if ($LASTEXITCODE -ne 0) { throw "signtool failed" }

# xbox needs the VCLibs appx installed as a dependency
$vclibs = Get-ChildItem (Join-Path ${env:ProgramFiles(x86)} "Microsoft SDKs\Windows Kits\10\ExtensionSDKs\Microsoft.VCLibs\14.0\Appx\Retail\x64") -Filter *.appx -ErrorAction SilentlyContinue | Select-Object -First 1
if ($vclibs) { Copy-Item $vclibs.FullName $OutDir } else { Write-Warning "VCLibs appx not found" }

Write-Host "Done:"
Get-ChildItem $OutDir | ForEach-Object { Write-Host "  $($_.Name)" }
