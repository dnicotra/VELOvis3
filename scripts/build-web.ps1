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

    # emcmake needs Ninja or mingw32-make; neither ships on PATH by default, but
    # emsdk bundles Ninja under upstream\bin, so fall back to that.
    $generatorArgs = @()
    if (-not (Get-Command ninja -ErrorAction SilentlyContinue) -and -not (Get-Command mingw32-make -ErrorAction SilentlyContinue)) {
        $emsdkRoot = $env:EMSDK
        $bundledNinja = if ($emsdkRoot) { Join-Path $emsdkRoot "upstream\bin\ninja.exe" } else { $null }
        if ($bundledNinja -and (Test-Path $bundledNinja)) {
            $env:PATH = "$(Split-Path $bundledNinja);$env:PATH"
            $generatorArgs = @("-G", "Ninja")
        } else {
            Write-Error "No CMake generator found (ninja/mingw32-make). Install Ninja or pass -EmsdkPath so its bundled copy can be used."
        }
    }

    emcmake cmake -B build-web -S . -DCMAKE_BUILD_TYPE=Release @generatorArgs
    cmake --build build-web --parallel
    if ($Serve) {
        Write-Host "Serving on http://localhost:8080/VELOvis3.html"
        Push-Location build-web
        python -m http.server 8080
        Pop-Location
    }
} finally {
    Pop-Location
}
