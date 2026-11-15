# makes the trimmed java runtime + lwjgl + the test classes for the app
# .\java\build.ps1 -Jdk "C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot"
# output in staging\java:
#   jre\       jlink runtime
#   lib\       lwjgl jars
#   natives\   lwjgl natives (glfw is my own glfw3.dll, not lwjgl's)
#   classes\   test classes
#   include\   jni headers for the host

param(
    [Parameter(Mandatory = $true)] [string] $Jdk
)

$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$root = Split-Path $here
$out = Join-Path $root "staging\java"
$dl = Join-Path $root "staging\dl"
$Jdk = (Resolve-Path $Jdk).Path

$ver = (Get-Content (Join-Path $Jdk "release") | Where-Object { $_ -like "JAVA_VERSION=*" }) -replace '.*="?([^"]*)"?', '$1'
Write-Host "JDK $ver at $Jdk"
if (-not $ver.StartsWith("17")) { throw "need a JDK 17 (minecraft 1.16.5 + fabric run on 17)" }

if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out, $dl | Out-Null

# what 1.16.5 + fabric + lwjgl need, nothing for desktop awt windows
$modules = @(
    "java.base", "java.desktop", "java.logging", "java.management", "java.naming",
    "java.net.http", "java.scripting", "java.sql", "java.xml", "jdk.crypto.ec",
    "jdk.unsupported", "jdk.zipfs", "jdk.localedata"
) -join ","

& (Join-Path $Jdk "bin\jlink.exe") --module-path (Join-Path $Jdk "jmods") --add-modules $modules `
    --include-locales en --strip-debug --no-header-files --no-man-pages --compress=0 `
    --output (Join-Path $out "jre")
if ($LASTEXITCODE -ne 0) { throw "jlink failed" }

# lwjgl, same version minecraft 1.16.5 uses. sha1 checked against maven central
$lwjgl = "3.2.2"
$maven = "https://repo1.maven.org/maven2/org/lwjgl"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
function Get-Jar($artifact, $classifier) {
    $name = if ($classifier) { "$artifact-$lwjgl-$classifier.jar" } else { "$artifact-$lwjgl.jar" }
    $file = Join-Path $dl $name
    $url = "$maven/$artifact/$lwjgl/$name"
    $want = (Invoke-WebRequest -UseBasicParsing "$url.sha1").Content
    if ($want -is [byte[]]) { $want = [Text.Encoding]::ASCII.GetString($want) }
    $want = $want.Trim().Split(" ")[0].ToLower()
    if (-not (Test-Path $file) -or (Get-FileHash -Algorithm SHA1 $file).Hash.ToLower() -ne $want) {
        Write-Host "downloading $name"
        Invoke-WebRequest -UseBasicParsing $url -OutFile $file
        if ((Get-FileHash -Algorithm SHA1 $file).Hash.ToLower() -ne $want) { throw "sha1 mismatch for $name" }
    }
    return $file
}

$lib = Join-Path $out "lib"
$natives = Join-Path $out "natives"
New-Item -ItemType Directory -Force $lib, $natives | Out-Null
foreach ($a in @("lwjgl", "lwjgl-glfw", "lwjgl-opengl")) { Copy-Item (Get-Jar $a "") $lib }

Add-Type -AssemblyName System.IO.Compression.FileSystem
# minecraft also wants stb (textures), openal (sound), tinyfd and jemalloc
foreach ($a in @("lwjgl", "lwjgl-opengl", "lwjgl-stb", "lwjgl-openal", "lwjgl-tinyfd", "lwjgl-jemalloc")) {
    $zip = [IO.Compression.ZipFile]::OpenRead((Get-Jar $a "natives-windows"))
    try {
        foreach ($e in $zip.Entries) {
            # 3.2.2 keeps the x64 dlls in the jar root, *32.dll are the 32 bit ones
            if ($e.FullName -match '^[a-z_]+\.dll$' -and $e.FullName -notmatch '32\.dll$') {
                [IO.Compression.ZipFileExtensions]::ExtractToFile($e, (Join-Path $natives $e.Name), $true)
            }
        }
    } finally { $zip.Dispose() }
}
Write-Host "lwjgl natives: $((Get-ChildItem $natives).Name -join ', ')"

# test classes
$classes = Join-Path $out "classes"
$cp = (Get-ChildItem $lib -Filter *.jar).FullName -join ";"
& (Join-Path $Jdk "bin\javac.exe") -encoding UTF-8 -cp $cp -d $classes (Get-ChildItem $here -Filter *.java).FullName
if ($LASTEXITCODE -ne 0) { throw "javac failed" }

# jdk fixes for the app sandbox (patches in java\jdk-patches, against the jdk's
# own src.zip). toRealPath walks every folder from C:\ and the sandbox can't look
# into C:\Users\<me>. the classes get loaded with --patch-module java.base
$psrc = Join-Path $out "jdk-src"
$pout = Join-Path $out "patch\java.base"
New-Item -ItemType Directory -Force $psrc, $pout | Out-Null
$srcZip = [IO.Compression.ZipFile]::OpenRead((Join-Path $Jdk "lib\src.zip"))
$patched = @()
try {
    foreach ($p in Get-ChildItem (Join-Path $here "jdk-patches") -Filter *.patch) {
        foreach ($f in (Select-String -Path $p.FullName -Pattern '^\+\+\+ b/(.+)$').Matches) {
            $rel = $f.Groups[1].Value
            $dest = Join-Path $psrc $rel
            New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($srcZip.GetEntry($rel), $dest, $true)
            $patched += $dest
        }
    }
} finally { $srcZip.Dispose() }
$git = (Get-Command git.exe -All | Where-Object { $_.Source -like "*.exe" } | Select-Object -First 1).Source
# jdk-src sits inside this repo, so git apply needs --directory (from the repo
# root), otherwise it quietly skips the file
foreach ($p in Get-ChildItem (Join-Path $here "jdk-patches") -Filter *.patch) {
    Push-Location $root
    & $git apply --whitespace=nowarn --directory=staging/java/jdk-src $p.FullName
    $ok = $LASTEXITCODE -eq 0
    Pop-Location
    if (-not $ok) { throw "jdk patch $($p.Name) failed" }
}
foreach ($f in $patched) {
    if (-not (Select-String -Path $f -SimpleMatch "xbox-mc" -Quiet)) { throw "jdk patch didn't change $f" }
}
& (Join-Path $Jdk "bin\javac.exe") --patch-module "java.base=$(Join-Path $psrc 'java.base')" -d $pout $patched
if ($LASTEXITCODE -ne 0) { throw "jdk patch compile failed" }
Write-Host "jdk patches: $((Get-ChildItem -Recurse $pout -Filter *.class).Count) classes"

$inc = Join-Path $out "include"
New-Item -ItemType Directory -Force $inc | Out-Null
Copy-Item (Join-Path $Jdk "include\jni.h") $inc
Copy-Item (Join-Path $Jdk "include\win32\jni_md.h") $inc

$mb = [int]((Get-ChildItem -Recurse (Join-Path $out "jre") | Measure-Object Length -Sum).Sum / 1MB)
Write-Host "jre is $mb MB"
