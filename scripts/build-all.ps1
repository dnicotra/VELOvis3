# Build both native (desktop) and Emscripten (web) targets — Windows / PowerShell
# Usage:
#   .\scripts\build-all.ps1 [-EmsdkPath C:\emsdk]
param(
    [string]$EmsdkPath = $env:EMSDK
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

Write-Host "=== Building desktop (native) ===" -ForegroundColor Cyan
& "$root\scripts\build-desktop.ps1"

Write-Host "=== Building web (Emscripten) ===" -ForegroundColor Cyan
& "$root\scripts\build-web.ps1" -EmsdkPath $EmsdkPath

Write-Host "=== Done ===" -ForegroundColor Green
Write-Host "Desktop exe: build-desktop\Release\VELOvis3.exe"
Write-Host "Web output:  build-web\VELOvis3.html (serve with: cd build-web; python -m http.server 8080)"
