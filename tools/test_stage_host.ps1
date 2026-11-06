param([string]$Compiler = (Join-Path $PSScriptRoot '../artifacts/host-tools/tcc/tcc.exe'))
$ErrorActionPreference='Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    New-Item -ItemType Directory -Force artifacts | Out-Null
    python tools/generate_ui_font.py --check
    if($LASTEXITCODE -ne 0){throw 'Font atlas verification failed'}
    if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Provide -Compiler with a native Windows TinyCC executable. See docs/stage_firmware.md.' }
    & $Compiler -shared -rdynamic -Itests/host/include -Icomponents/godox_waveform/include -Icomponents/godox_protocol/include -Icomponents/stage_control/include -Icomponents/stage_ui/include -Icomponents/stage_ui components/godox_waveform/godox_dsp.c components/godox_protocol/godox_scan.c components/godox_protocol/godox_protocol.c components/godox_protocol/godox_receive.c components/stage_control/stage_core.c components/stage_control/stage_timing.c components/stage_control/stage_cache_plan.c components/stage_control/stage_workspace.c tests/host/cache_heap_guard.c components/stage_control/joint_cache.c tests/host/mock_partition.c components/stage_ui/stage_view.c components/stage_ui/stage_frame.c components/stage_ui/stage_epd.c components/stage_ui/stage_power.c tests/host/mock_gpio.c -o artifacts/stage_host.dll
    if($LASTEXITCODE -ne 0){throw 'Native C compilation failed'}
    & $Compiler -shared -rdynamic -Itests/host/pipeline_include -Itests/host/include -Icomponents/rf_transmitter/include -Icomponents/godox_protocol/include -Icomponents/stage_control tests/host/mock_rx_pipeline.c components/stage_control/stage_rx_pipeline.c components/godox_protocol/godox_scan.c components/godox_protocol/godox_protocol.c -o artifacts/rx_pipeline_host.dll
    if($LASTEXITCODE -ne 0){throw 'Pipeline host compilation failed'}
    python -m unittest discover -s tests -v
    if($LASTEXITCODE -ne 0){throw 'Host verification failed'}
} finally {Pop-Location}
