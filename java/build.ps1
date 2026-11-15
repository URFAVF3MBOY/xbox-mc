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
    "jdk.unsupported", "jdk.zipfs", "jdk.localedata",
    # mixin (fabric) uses javax.annotation.processing from here
    "java.compiler"
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
# jna's dll (minecraft uses jna for the narrator and system info). jna normally
# unpacks it to temp and loads it from there, a uwp app can only load dlls from
# its own package, so it ships in natives\. same version minecraft 1.17.1 uses
$jnaVer = "5.8.0"
$jnaJar = Join-Path $dl "jna-$jnaVer.jar"
$jnaUrl = "https://repo1.maven.org/maven2/net/java/dev/jna/jna/$jnaVer/jna-$jnaVer.jar"
$want = (Invoke-WebRequest -UseBasicParsing "$jnaUrl.sha1").Content
if ($want -is [byte[]]) { $want = [Text.Encoding]::ASCII.GetString($want) }
$want = $want.Trim().Split(" ")[0].ToLower()
if (-not (Test-Path $jnaJar) -or (Get-FileHash -Algorithm SHA1 $jnaJar).Hash.ToLower() -ne $want) {
    Invoke-WebRequest -UseBasicParsing $jnaUrl -OutFile $jnaJar
    if ((Get-FileHash -Algorithm SHA1 $jnaJar).Hash.ToLower() -ne $want) { throw "sha1 mismatch for jna" }
}
$zip = [IO.Compression.ZipFile]::OpenRead($jnaJar)
try {
    [IO.Compression.ZipFileExtensions]::ExtractToFile($zip.GetEntry("com/sun/jna/win32-x86-64/jnidispatch.dll"), (Join-Path $natives "jnidispatch.dll"), $true)
} finally { $zip.Dispose() }

Write-Host "natives: $((Get-ChildItem $natives).Name -join ', ')"

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
# through cmd: javac warns about internal api (it's the jdk itself), and
# powershell 5 treats anything on stderr as an error
$javac = Join-Path $Jdk "bin\javac.exe"
$files = ($patched | ForEach-Object { "`"$_`"" }) -join " "
$jout = cmd /c "`"$javac`" -nowarn --patch-module `"java.base=$(Join-Path $psrc 'java.base')`" -d `"$pout`" $files 2>&1"
if ($LASTEXITCODE -ne 0) { $jout | Write-Host; throw "jdk patch compile failed" }
Write-Host "jdk patches: $((Get-ChildItem -Recurse $pout -Filter *.class).Count) classes"

# my compat mod (compatmod\): mixins that make sodium work on the xbox one.
# sodium and mixin are only needed to compile it, they don't go in the package
function Get-Checked($url, $sha1, $file) {
    if (-not (Test-Path $file) -or (Get-FileHash -Algorithm SHA1 $file).Hash.ToLower() -ne $sha1) {
        Invoke-WebRequest -UseBasicParsing $url -OutFile $file
        if ((Get-FileHash -Algorithm SHA1 $file).Hash.ToLower() -ne $sha1) { throw "sha1 mismatch for $url" }
    }
    return $file
}
$sodiumVer = "mc1.17.1-0.3.4"
$versions = Invoke-RestMethod -UseBasicParsing -UserAgent "xbox-mc/0.1" "https://api.modrinth.com/v2/project/sodium/version"
$sv = $versions | Where-Object { $_.version_number -eq $sodiumVer } | Select-Object -First 1
if (-not $sv) { throw "sodium $sodiumVer not on modrinth" }
$sf = $sv.files | Select-Object -First 1
$sodiumJar = Get-Checked $sf.url $sf.hashes.sha1 (Join-Path $dl $sf.filename)
$mixinVer = "0.17.5+mixin.0.8.7"
$mixinUrl = "https://maven.fabricmc.net/net/fabricmc/sponge-mixin/$mixinVer/sponge-mixin-$mixinVer.jar"
$mixinSha = ((Invoke-WebRequest -UseBasicParsing "$mixinUrl.sha1").Content)
if ($mixinSha -is [byte[]]) { $mixinSha = [Text.Encoding]::ASCII.GetString($mixinSha) }
$mixinJar = Get-Checked $mixinUrl $mixinSha.Trim().Split(" ")[0].ToLower() (Join-Path $dl "sponge-mixin-$mixinVer.jar")

$cmSrc = Join-Path $root "compatmod"
$cmOut = Join-Path $out "compatmod-classes"
New-Item -ItemType Directory -Force $cmOut | Out-Null
$cmCp = (@($sodiumJar, $mixinJar) + (Get-ChildItem $lib -Filter *.jar).FullName) -join ";"
# -proc:none: the mixin jar brings an annotation processor (refmaps), not needed
# here since nothing points at minecraft's own names
& (Join-Path $Jdk "bin\javac.exe") -encoding UTF-8 --release 16 -proc:none -cp $cmCp -d $cmOut (Get-ChildItem -Recurse (Join-Path $cmSrc "src") -Filter *.java).FullName
if ($LASTEXITCODE -ne 0) { throw "compat mod javac failed" }
Copy-Item (Join-Path $cmSrc "resources\*") $cmOut
$mods = Join-Path $out "mods"
New-Item -ItemType Directory -Force $mods | Out-Null
& (Join-Path $Jdk "bin\jar.exe") --create --file (Join-Path $mods "xbox-mc-compat.jar") -C $cmOut .
if ($LASTEXITCODE -ne 0) { throw "compat mod jar failed" }
Write-Host "compat mod: $((Get-Item (Join-Path $mods 'xbox-mc-compat.jar')).Length) bytes"

$inc = Join-Path $out "include"
New-Item -ItemType Directory -Force $inc | Out-Null
Copy-Item (Join-Path $Jdk "include\jni.h") $inc
Copy-Item (Join-Path $Jdk "include\win32\jni_md.h") $inc

$mb = [int]((Get-ChildItem -Recurse (Join-Path $out "jre") | Measure-Object Length -Sum).Sum / 1MB)
Write-Host "jre is $mb MB"
