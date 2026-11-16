# dev only bench mod, built on my pc against the remapped game jar (not in ci,
# not shipped). puts the jar into the registered staging\package\mods
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$jdk = "C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot"
$game = "$env:LOCALAPPDATA\Packages\xbox-mc_n1bqpkrcfn9ng\LocalState\game"
$mcJar = "$game\.fabric\remappedJars\minecraft-1.17.1-0.20.1\client-intermediary.jar"
$mixin = (Get-ChildItem (Join-Path (Split-Path $here) "staging\dl") -Filter "sponge-mixin-*.jar" | Select -First 1).FullName
$out = Join-Path (Split-Path $here) "staging\benchmod"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null
& "$jdk\bin\javac.exe" -encoding UTF-8 --release 16 -proc:none -cp "$mcJar;$mixin" -d $out (Get-ChildItem -Recurse "$here\src" -Filter *.java).FullName
if ($LASTEXITCODE -ne 0) { throw "javac failed" }
Copy-Item "$here\resources\*" $out
$benchJar = "$(Split-Path $here)\staging\package\mods\1.17.1\xbox-mc-bench.jar"
& "$jdk\bin\jar.exe" --create --file $benchJar -C $out .
Write-Host "bench mod in staging\package\mods\1.17.1 (the launcher copies it into game\mods)"
# jar.exe writes through a java temp file, which ends up owner-only and the app
# can't read it. give it the folder's normal permissions back
icacls $benchJar /reset | Out-Null
