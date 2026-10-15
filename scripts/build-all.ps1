# StreamX — build completa: libobs (OBS 32.2.2) -> StreamX -> instalador
# Requisitos: Visual Studio 2022 Build Tools (C++ + Windows SDK 22621+), Git, CMake 3.28–3.30 (tools\cmake) e NSIS 3.
param([switch]$SkipObs, [string]$Makensis = "$env:LOCALAPPDATA\electron-builder\Cache\nsis\nsis-3.0.4.1\Bin\makensis.exe")
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cmake = Join-Path $root 'tools\cmake\bin\cmake.exe'
if (-not (Test-Path $cmake)) { $cmake = 'cmake' }
$obs = Join-Path $root 'deps\obs-studio'

if (-not $SkipObs) {
  if (-not (Test-Path $obs)) {
    git clone --depth 1 --branch 32.2.2 --recurse-submodules --shallow-submodules https://github.com/obsproject/obs-studio.git $obs
  }
  Copy-Item (Join-Path $root 'scripts\CMakeUserPresets.json') (Join-Path $obs 'CMakeUserPresets.json') -Force
  Push-Location $obs
  & $cmake --preset streamx; if ($LASTEXITCODE) { throw 'configuração do OBS falhou' }
  & $cmake --build --preset streamx --parallel; if ($LASTEXITCODE) { throw 'build do OBS falhou' }
  & $cmake --install build_streamx --config RelWithDebInfo --prefix ..\obs-install; if ($LASTEXITCODE) { throw 'install do OBS falhou' }
  Pop-Location
}

& (Join-Path $PSScriptRoot 'stage.ps1')
& $cmake -S $root -B (Join-Path $root 'build') -G 'Visual Studio 17 2022' -A x64; if ($LASTEXITCODE) { throw 'configuração do StreamX falhou' }
& $cmake --build (Join-Path $root 'build') --config RelWithDebInfo --parallel; if ($LASTEXITCODE) { throw 'build do StreamX falhou' }

New-Item -ItemType Directory -Force (Join-Path $root 'dist') | Out-Null
& $Makensis /V2 /DVERSION=1.0.0 (Join-Path $root 'installer\streamx.nsi'); if ($LASTEXITCODE) { throw 'instalador falhou' }
Write-Host "Pronto: $(Join-Path $root 'dist\StreamX-Setup-1.0.0.exe')"
