param([string]$IdfPath = $env:IDF_PATH)
$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
if (-not $IdfPath) { throw 'Set IDF_PATH to your ESP-IDF v5.5.5 installation.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    . (Join-Path $IdfPath 'export.ps1')
    python (Join-Path $IdfPath 'tools/idf.py') -DIDF_TARGET=esp32s3 build
    if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF build failed' }
    python tools/check_elf.py build/Godox-S3-Trigger.elf --waves components/godox_waveform/waves
    if ($LASTEXITCODE -ne 0) { throw 'RF memory validation failed' }
} finally { Pop-Location }
