# makes the trimmed java runtime + lwjgl + the test classes for the app
# .\java\build.ps1 -Jdk "C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot"
# output in staging\java:
#   jre\       jlink runtime
#   lib\       lwjgl jars
#   natives\   lwjgl.dll, lwjgl_opengl.dll (glfw is my own glfw3.dll, not lwjgl's)
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
foreach ($a in @("lwjgl", "lwjgl-opengl")) {
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

$inc = Join-Path $out "include"
New-Item -ItemType Directory -Force $inc | Out-Null
Copy-Item (Join-Path $Jdk "include\jni.h") $inc
Copy-Item (Join-Path $Jdk "include\win32\jni_md.h") $inc

$mb = [int]((Get-ChildItem -Recurse (Join-Path $out "jre") | Measure-Object Length -Sum).Sum / 1MB)
Write-Host "jre is $mb MB"
