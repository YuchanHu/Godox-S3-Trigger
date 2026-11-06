param(
    [string]$Port,
    [ValidateRange(1,120)][int]$Seconds = 60,
    [ValidateRange(0,32)][int]$Channel = 0,
    [switch]$DumpOnly
)
$ErrorActionPreference = 'Stop'
$pythonPath = Join-Path $env:USERPROFILE '.espressif/python_env/idf5.5_py3.10_env/Scripts/python.exe'
if (-not (Test-Path -LiteralPath $pythonPath)) {
    $pythonPath = (Get-Command python -ErrorAction Stop).Source
}
$arguments = @((Join-Path $PSScriptRoot 'capture_rx_log.py'), '--seconds', $Seconds)
if ($Channel) { $arguments += @('--channel', $Channel) }
if ($Port) { $arguments += @('--port', $Port) }
if ($DumpOnly) { $arguments += '--dump-only' }
& $pythonPath @arguments
exit $LASTEXITCODE
