# Web (Emscripten) build — Windows / PowerShell
# Requires emsdk to be activated in this session, or pass -EmsdkPath to source it.
# Usage:
#   .\scripts\build-web.ps1 [-EmsdkPath C:\emsdk] [-Serve]
param(
    [string]$EmsdkPath = $env:EMSDK,
    [switch]$Serve
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    if (-not (Get-Command emcmake -ErrorAction SilentlyContinue)) {
        if ($EmsdkPath -and (Test-Path "$EmsdkPath\emsdk_env.ps1")) {
            Write-Host "Activating emsdk at $EmsdkPath"
            & "$EmsdkPath\emsdk_env.ps1"
        } else {
            Write-Error "emcmake not found. Activate emsdk first or pass -EmsdkPath."
        }
    }
    emcmake cmake -B build-web -S . -DCMAKE_BUILD_TYPE=Release
    cmake --build build-web --parallel
    if ($Serve) {
        Write-Host "Serving on http://localhost:8080/dear_raylib.html"
        Push-Location build-web
        python -m http.server 8080
        Pop-Location
    }
} finally {
    Pop-Location
}
