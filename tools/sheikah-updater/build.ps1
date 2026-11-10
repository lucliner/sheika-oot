# Builds sheikah-updater.exe (needs only llvm-mingw: HOOKSHOT_MINGW, or ..\..\..\tools\llvm-mingw next to the checkout)
$mingw = $env:HOOKSHOT_MINGW
if (-not $mingw) { $mingw = Join-Path $PSScriptRoot "..\..\..\tools\llvm-mingw" }
$cc = Join-Path $mingw "bin\x86_64-w64-mingw32-clang.exe"
if (-not (Test-Path $cc)) { throw "llvm-mingw not found: set HOOKSHOT_MINGW" }
Push-Location $PSScriptRoot
try {
    & $cc -O2 -s -mwindows -municode -Wall -o sheikah-updater.exe sheikah_updater.c -lcomctl32 -lshell32 -lole32 -luuid
    if ($LASTEXITCODE) { throw "build failed" }
    "built $PSScriptRoot\sheikah-updater.exe"
} finally { Pop-Location }
