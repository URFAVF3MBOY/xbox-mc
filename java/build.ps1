# makes the java runtimes + lwjgl + the launcher classes for the app
# .\java\build.ps1 -Jdk "C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot" `
#                  -Jdk21 "C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot"
# two runtimes, one per minecraft generation (the app restarts into the right one):
#   1.17.1:  jre\   patch\java.base    lib\   natives\     (java 17, lwjgl 3.2.2, jna 5.8.0)
#   1.21.11: jre21\ patch21\java.base  lib21\ natives21\   (java 21, lwjgl 3.3.3, jna 5.17.0)
# output in staging\java, plus:
#   classes\   launcher classes (built for java 17, run on both)
#   mods\      my compat mod (1.17.1 only)
#   include\   jni headers for the host

param(
    [Parameter(Mandatory = $true)] [string] $Jdk,
    [Parameter(Mandatory = $true)] [string] $Jdk21
)

$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$root = Split-Path $here
$out = Join-Path $root "staging\java"
$dl = Join-Path $root "staging\dl"
$Jdk = (Resolve-Path $Jdk).Path
$Jdk21 = (Resolve-Path $Jdk21).Path

function Get-JdkVersion($jdk) {
    (Get-Content (Join-Path $jdk "release") | Where-Object { $_ -like "JAVA_VERSION=*" }) -replace '.*="?([^"]*)"?', '$1'
}
$ver = Get-JdkVersion $Jdk
$ver21 = Get-JdkVersion $Jdk21
Write-Host "JDK $ver at $Jdk"
Write-Host "JDK $ver21 at $Jdk21"
if (-not $ver.StartsWith("17")) { throw "-Jdk has to be a JDK 17 (minecraft 1.17.1)" }
if (-not $ver21.StartsWith("21")) { throw "-Jdk21 has to be a JDK 21 (minecraft 1.21.11)" }

if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out, $dl | Out-Null
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.IO.Compression.FileSystem

# downloads a maven file, sha1 checked against the .sha1 next to it
function Get-Maven($url) {
    $file = Join-Path $dl ($url -replace '.*/', '')
    $want = (Invoke-WebRequest -UseBasicParsing "$url.sha1").Content
    if ($want -is [byte[]]) { $want = [Text.Encoding]::ASCII.GetString($want) }
    $want = $want.Trim().Split(" ")[0].ToLower()
    if (-not (Test-Path $file) -or (Get-FileHash -Algorithm SHA1 $file).Hash.ToLower() -ne $want) {
        Write-Host "downloading $($url -replace '.*/', '')"
        Invoke-WebRequest -UseBasicParsing -UserAgent "xbox-mc/0.1" $url -OutFile $file
        if ((Get-FileHash -Algorithm SHA1 $file).Hash.ToLower() -ne $want) { throw "sha1 mismatch for $url" }
    }
    return $file
}

# ---- jre (jlink) ----
# what minecraft + fabric + lwjgl need, nothing for desktop awt windows
$modules = @(
    "java.base", "java.desktop", "java.logging", "java.management", "java.naming",
    "java.net.http", "java.scripting", "java.sql", "java.xml", "jdk.crypto.ec",
    "jdk.unsupported", "jdk.zipfs", "jdk.localedata",
    # mixin (fabric) uses javax.annotation.processing from here
    "java.compiler",
    # flight recorder, only used for the profiles from the settings page
    "jdk.jfr"
) -join ","
function New-Jre($jdk, $name) {
    # uncompressed: a compressed image crashes the jvm at startup on the xbox.
    # jdk 21 calls that zip-0 and warns about the old spelling
    $compress = if ((Get-JdkVersion $jdk).StartsWith("17")) { "--compress=0" } else { "--compress=zip-0" }
    & (Join-Path $jdk "bin\jlink.exe") --module-path (Join-Path $jdk "jmods") --add-modules $modules `
        --include-locales en --strip-debug --no-header-files --no-man-pages $compress `
        --output (Join-Path $out $name)
    if ($LASTEXITCODE -ne 0) { throw "jlink failed for $name" }
    $mb = [int]((Get-ChildItem -Recurse (Join-Path $out $name) | Measure-Object Length -Sum).Sum / 1MB)
    Write-Host "$name is $mb MB"
}

# ---- lwjgl + jna ----
# the jars the app loads itself (core, glfw, opengl), and the natives for every
# lwjgl module minecraft uses. glfw is my own glfw3.dll, not lwjgl's.
# jna's dll ships too: jna normally unpacks it to temp and loads it from there,
# a uwp app can only load dlls from its own package
function New-Lwjgl($lwjgl, $jna, $libName, $nativesName, $modulesWithNatives) {
    $lib = Join-Path $out $libName
    $natives = Join-Path $out $nativesName
    New-Item -ItemType Directory -Force $lib, $natives | Out-Null
    $maven = "https://repo1.maven.org/maven2/org/lwjgl"
    foreach ($a in @("lwjgl", "lwjgl-glfw", "lwjgl-opengl")) { Copy-Item (Get-Maven "$maven/$a/$lwjgl/$a-$lwjgl.jar") $lib }
    foreach ($a in $modulesWithNatives) {
        $zip = [IO.Compression.ZipFile]::OpenRead((Get-Maven "$maven/$a/$lwjgl/$a-$lwjgl-natives-windows.jar"))
        try {
            foreach ($e in $zip.Entries) {
                # 3.2.2 keeps the x64 dlls in the jar root (*32.dll are 32 bit),
                # 3.3.x under windows/x64/org/lwjgl/(module/)
                if (($e.FullName -match '^[a-z_]+\.dll$' -and $e.FullName -notmatch '32\.dll$') -or
                    $e.FullName -match '^windows/x64/org/lwjgl/([a-z_]+/)?[a-z_]+\.dll$') {
                    [IO.Compression.ZipFileExtensions]::ExtractToFile($e, (Join-Path $natives $e.Name), $true)
                }
            }
        } finally { $zip.Dispose() }
    }
    $zip = [IO.Compression.ZipFile]::OpenRead((Get-Maven "https://repo1.maven.org/maven2/net/java/dev/jna/jna/$jna/jna-$jna.jar"))
    try {
        [IO.Compression.ZipFileExtensions]::ExtractToFile($zip.GetEntry("com/sun/jna/win32-x86-64/jnidispatch.dll"), (Join-Path $natives "jnidispatch.dll"), $true)
    } finally { $zip.Dispose() }
    Write-Host "$nativesName`: $((Get-ChildItem $natives).Name -join ', ')"
}

# ---- jdk fixes for the app sandbox ----
# patches in java\jdk-patches, against the jdk's own src.zip (they apply to 17
# and 21). toRealPath walks every folder from C:\ and the sandbox can't look into
# C:\Users\<me>. the classes get loaded with --patch-module java.base
$git = (Get-Command git.exe -All | Where-Object { $_.Source -like "*.exe" } | Select-Object -First 1).Source
function New-JdkPatch($jdk, $srcName, $patchName) {
    $psrc = Join-Path $out $srcName
    $pout = Join-Path $out "$patchName\java.base"
    New-Item -ItemType Directory -Force $psrc, $pout | Out-Null
    $srcZip = [IO.Compression.ZipFile]::OpenRead((Join-Path $jdk "lib\src.zip"))
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
    # the source sits inside this repo, so git apply needs --directory (from the
    # repo root), otherwise it quietly skips the file
    foreach ($p in Get-ChildItem (Join-Path $here "jdk-patches") -Filter *.patch) {
        Push-Location $root
        & $git apply --whitespace=nowarn --directory=staging/java/$srcName $p.FullName
        $ok = $LASTEXITCODE -eq 0
        Pop-Location
        if (-not $ok) { throw "jdk patch $($p.Name) failed for $srcName" }
    }
    foreach ($f in $patched) {
        if (-not (Select-String -Path $f -SimpleMatch "xbox-mc" -Quiet)) { throw "jdk patch didn't change $f" }
    }
    # through cmd: javac warns about internal api (it's the jdk itself), and
    # powershell 5 treats anything on stderr as an error
    $javac = Join-Path $jdk "bin\javac.exe"
    $files = ($patched | ForEach-Object { "`"$_`"" }) -join " "
    $jout = cmd /c "`"$javac`" -nowarn --patch-module `"java.base=$(Join-Path $psrc 'java.base')`" -d `"$pout`" $files 2>&1"
    if ($LASTEXITCODE -ne 0) { $jout | Write-Host; throw "jdk patch compile failed for $patchName" }
    Write-Host "$patchName`: $((Get-ChildItem -Recurse $pout -Filter *.class).Count) classes"
}

# minecraft 1.17.1: java 17, lwjgl 3.2.2, jna 5.8.0
New-Jre $Jdk "jre"
New-Lwjgl "3.2.2" "5.8.0" "lib" "natives" @("lwjgl", "lwjgl-opengl", "lwjgl-stb", "lwjgl-openal", "lwjgl-tinyfd", "lwjgl-jemalloc")
New-JdkPatch $Jdk "jdk-src" "patch"

# minecraft 1.21.11: java 21, lwjgl 3.3.3 (adds freetype), jna 5.17.0
New-Jre $Jdk21 "jre21"
New-Lwjgl "3.3.3" "5.17.0" "lib21" "natives21" @("lwjgl", "lwjgl-opengl", "lwjgl-stb", "lwjgl-openal", "lwjgl-tinyfd", "lwjgl-jemalloc", "lwjgl-freetype")
New-JdkPatch $Jdk21 "jdk21-src" "patch21"

# launcher + test classes, against lwjgl 3.2.2 for java 17. the few lwjgl calls
# the launcher makes are the same in 3.3.3, so the classes run on both runtimes
$classes = Join-Path $out "classes"
$cp = (Get-ChildItem (Join-Path $out "lib") -Filter *.jar).FullName -join ";"
& (Join-Path $Jdk "bin\javac.exe") -encoding UTF-8 --release 17 -cp $cp -d $classes (Get-ChildItem $here -Filter *.java).FullName
if ($LASTEXITCODE -ne 0) { throw "javac failed" }

# my compat mod (compatmod\): mixins that make sodium work on the xbox one.
# sodium and mixin are only needed to compile it, they don't go in the package
function Get-Checked($url, $sha1, $file) {
    if (-not (Test-Path $file) -or (Get-FileHash -Algorithm SHA1 $file).Hash.ToLower() -ne $sha1) {
        Invoke-WebRequest -UseBasicParsing -UserAgent "xbox-mc/0.1" $url -OutFile $file
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
$mixinJar = Get-Maven "https://maven.fabricmc.net/net/fabricmc/sponge-mixin/0.17.5+mixin.0.8.7/sponge-mixin-0.17.5+mixin.0.8.7.jar"

$cmSrc = Join-Path $root "compatmod"
$cmOut = Join-Path $out "compatmod-classes"
New-Item -ItemType Directory -Force $cmOut | Out-Null
$cmCp = (@($sodiumJar, $mixinJar) + (Get-ChildItem (Join-Path $out "lib") -Filter *.jar).FullName) -join ";"
# -proc:none: the mixin jar brings an annotation processor (refmaps), not needed
# here since nothing points at minecraft's own names
& (Join-Path $Jdk "bin\javac.exe") -encoding UTF-8 --release 16 -proc:none -cp $cmCp -d $cmOut (Get-ChildItem -Recurse (Join-Path $cmSrc "src") -Filter *.java).FullName
if ($LASTEXITCODE -ne 0) { throw "compat mod javac failed" }
Copy-Item (Join-Path $cmSrc "resources\*") $cmOut
# mods\<minecraft version>\ = my mods the launcher copies into that version's mods folder
$mods = Join-Path $out "mods\1.17.1"
New-Item -ItemType Directory -Force $mods | Out-Null
& (Join-Path $Jdk "bin\jar.exe") --create --file (Join-Path $mods "xbox-mc-compat.jar") -C $cmOut .
if ($LASTEXITCODE -ne 0) { throw "compat mod jar failed" }
Write-Host "compat mod: $((Get-Item (Join-Path $mods 'xbox-mc-compat.jar')).Length) bytes"

# my compat mod for 1.21.11 (compatmod21\): lets controllable load SDL2 from the
# package. only needs mixin to compile
$cm21Src = Join-Path $root "compatmod21"
$cm21Out = Join-Path $out "compatmod21-classes"
New-Item -ItemType Directory -Force $cm21Out | Out-Null
& (Join-Path $Jdk "bin\javac.exe") -encoding UTF-8 --release 17 -proc:none -cp $mixinJar -d $cm21Out (Get-ChildItem -Recurse (Join-Path $cm21Src "src") -Filter *.java).FullName
if ($LASTEXITCODE -ne 0) { throw "1.21 compat mod javac failed" }
Copy-Item (Join-Path $cm21Src "resources\*") $cm21Out
$mods21 = Join-Path $out "mods\1.21.11"
New-Item -ItemType Directory -Force $mods21 | Out-Null
& (Join-Path $Jdk "bin\jar.exe") --create --file (Join-Path $mods21 "xbox-mc-compat-21.jar") -C $cm21Out .
if ($LASTEXITCODE -ne 0) { throw "1.21 compat mod jar failed" }

# SDL2.dll for controllable (1.21.11), taken from the same controllable jar the
# launcher downloads (pinned in Fabric.java). it goes in natives21 because the
# xbox can't load the copy controllable would unpack to temp
$ctrlJar = Get-Checked "https://mediafilez.forgecdn.net/files/7411/286/controllable-fabric-1.21.11-0.25.7.jar" `
    "f350119178e623cd648d3e5fd2d1e24a2e39863a" (Join-Path $dl "controllable-fabric-1.21.11-0.25.7.jar")
$zip = [IO.Compression.ZipFile]::OpenRead($ctrlJar)
try {
    $inner = Join-Path $dl "controllable-sdl-2.32.10-1.1.0.jar"
    [IO.Compression.ZipFileExtensions]::ExtractToFile($zip.GetEntry("META-INF/jars/controllable-sdl-2.32.10-1.1.0.jar"), $inner, $true)
} finally { $zip.Dispose() }
$zip = [IO.Compression.ZipFile]::OpenRead($inner)
try {
    [IO.Compression.ZipFileExtensions]::ExtractToFile($zip.GetEntry("win32-x86-64/SDL2.dll"), (Join-Path $out "natives21\SDL2.dll"), $true)
} finally { $zip.Dispose() }
Write-Host "SDL2.dll for controllable: $((Get-Item (Join-Path $out 'natives21\SDL2.dll')).Length) bytes"

$inc = Join-Path $out "include"
New-Item -ItemType Directory -Force $inc | Out-Null
Copy-Item (Join-Path $Jdk "include\jni.h") $inc
Copy-Item (Join-Path $Jdk "include\win32\jni_md.h") $inc
