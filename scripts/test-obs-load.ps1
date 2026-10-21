[CmdletBinding()]
param([string]$BuildPath = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
if (-not $BuildPath) {
    $candidates = @(
        (Join-Path $root 'build_x64/Release/module-load-test.exe'),
        (Join-Path $root 'build_local/module-load-test.exe')
    )
    $BuildPath = @($candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }) | Select-Object -First 1
}
if (-not $BuildPath -or -not (Test-Path -LiteralPath $BuildPath -PathType Leaf)) {
    throw 'module-load-test.exe was not found. Build the native targets first.'
}
$buildDirectory = Split-Path (Resolve-Path -LiteralPath $BuildPath).Path -Parent
$plugin = Join-Path $buildDirectory 'excelprotocol-fx.dll'
$runtime = Join-Path $root '.deps/obs-runtime/bin/64bit'
$data = Join-Path $root 'data'
if (-not (Test-Path -LiteralPath $plugin -PathType Leaf)) { throw "Plugin DLL missing: $plugin" }
if (-not (Test-Path -LiteralPath (Join-Path $runtime 'obs.dll') -PathType Leaf)) { throw 'Prepared OBS runtime is missing.' }
$logDirectory = Join-Path $root 'build-obs-smoke'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$log = Join-Path $logDirectory 'module-load.log'
$previousPath = $env:PATH
try {
    $env:PATH = "$runtime;$previousPath"
    $output = & $BuildPath $plugin $data 2>&1
    $exitCode = $LASTEXITCODE
    $output | Set-Content -LiteralPath $log -Encoding utf8
    if ($exitCode -ne 0) { throw "OBS module-load smoke failed with exit code $exitCode. See $log" }
    if (($output | Out-String) -notmatch 'registered its OBS audio/video source') { throw "OBS load evidence missing. See $log" }
} finally {
    $env:PATH = $previousPath
}
Write-Host "OBS module-load smoke passed. Log: $log"
