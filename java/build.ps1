# makes the trimmed java runtime + the test class for the app
# .\java\build.ps1 -Jdk "C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot"
# output in staging\java: jre\ (jlink), classes\, include\ (jni headers for the host)

param(
    [Parameter(Mandatory = $true)] [string] $Jdk
)

$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$root = Split-Path $here
$out = Join-Path $root "staging\java"
$Jdk = (Resolve-Path $Jdk).Path

$ver = (Get-Content (Join-Path $Jdk "release") | Where-Object { $_ -like "JAVA_VERSION=*" }) -replace '.*="?([^"]*)"?', '$1'
Write-Host "JDK $ver at $Jdk"
if (-not $ver.StartsWith("17")) { throw "need a JDK 17 (minecraft 1.16.5 + fabric run on 17)" }

if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null

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

$classes = Join-Path $out "classes"
& (Join-Path $Jdk "bin\javac.exe") -encoding UTF-8 -d $classes (Join-Path $here "Hello.java")
if ($LASTEXITCODE -ne 0) { throw "javac failed" }

$inc = Join-Path $out "include"
New-Item -ItemType Directory -Force $inc | Out-Null
Copy-Item (Join-Path $Jdk "include\jni.h") $inc
Copy-Item (Join-Path $Jdk "include\win32\jni_md.h") $inc

$mb = [int]((Get-ChildItem -Recurse (Join-Path $out "jre") | Measure-Object Length -Sum).Sum / 1MB)
Write-Host "jre is $mb MB"
