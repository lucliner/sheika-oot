# Builds both programs from sheikah_updater.c (needs only llvm-mingw: HOOKSHOT_MINGW, or ..\..\..\tools\llvm-mingw next to the checkout):
#   Sheikah-Builder.exe    what players get: builds the game from their own dump. Nothing about git or GitHub.
#   Sheikah-Publisher.exe  the developer's: also merges Hookshot's changes and publishes releases to GitHub. NOT shipped.
$mingw = $env:HOOKSHOT_MINGW
if (-not $mingw) { $mingw = Join-Path $PSScriptRoot "..\..\..\tools\llvm-mingw" }
$cc = Join-Path $mingw "bin\x86_64-w64-mingw32-clang.exe"
if (-not (Test-Path $cc)) { throw "llvm-mingw not found: set HOOKSHOT_MINGW" }
Push-Location $PSScriptRoot
try {
    $libs = "-lcomctl32", "-lshell32", "-lole32", "-luuid"
    & $cc -O2 -s -mwindows -municode -Wall -o Sheikah-Builder.exe sheikah_updater.c @libs
    if ($LASTEXITCODE) { throw "build failed" }
    & $cc -O2 -s -mwindows -municode -Wall -DPUBLISHER -o Sheikah-Publisher.exe sheikah_updater.c @libs
    if ($LASTEXITCODE) { throw "build failed" }
    "built Sheikah-Builder.exe and Sheikah-Publisher.exe in $PSScriptRoot"
} finally { Pop-Location }
