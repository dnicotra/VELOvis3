# Desktop (native) build — Windows / PowerShell
# Usage:  .\scripts\build-desktop.ps1 [-Run]
param([switch]$Run)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    cmake -B build-desktop -S . -DCMAKE_BUILD_TYPE=Release
    cmake --build build-desktop --config Release --parallel
    if ($Run) {
        $exe = Get-ChildItem -Path build-desktop -Recurse -Filter "VELOvis3.exe" | Select-Object -First 1
        if ($exe) { & $exe.FullName } else { Write-Error "Executable not found" }
    }
} finally {
    Pop-Location
}
