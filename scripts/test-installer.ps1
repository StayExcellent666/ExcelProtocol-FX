[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Installer,
    [string]$ObsRuntime = '.deps/obs-runtime',
    [string]$StagePath = 'stage'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$workspace = Split-Path $PSScriptRoot -Parent
$testRoot = Join-Path $workspace 'build-installer-smoke'
if (Test-Path -LiteralPath $testRoot) { throw "Use a clean build directory; test fixture already exists: $testRoot" }
$fixture = Join-Path $testRoot 'obs-studio'
$bin = Join-Path $fixture 'bin/64bit'
New-Item -ItemType Directory -Path $bin -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $ObsRuntime 'bin/64bit/obs64.exe') -Destination $bin
$installerPath = (Resolve-Path -LiteralPath $Installer).Path
$stage = (Resolve-Path -LiteralPath $StagePath).Path
$obsHash = (Get-FileHash -LiteralPath (Join-Path $bin 'obs64.exe') -Algorithm SHA256).Hash
$sentinel = Join-Path $fixture 'data/obs-plugins/unrelated-plugin/do-not-touch.txt'
New-Item -ItemType Directory -Path (Split-Path $sentinel -Parent) -Force | Out-Null
Set-Content -LiteralPath $sentinel -Value 'Unrelated OBS plugin data' -NoNewline

function Invoke-Installer([string]$TargetPath, [string]$Label, [bool]$ExpectSuccess) {
    $log = Join-Path $testRoot "$Label.log"
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/DIR=`"$TargetPath`"", "/LOG=`"$log`"")
    $process = Start-Process -FilePath $installerPath -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(120000)) { throw "Installer $Label timed out." }
    if (($process.ExitCode -eq 0) -ne $ExpectSuccess) { throw "Installer $Label returned $($process.ExitCode); see $log" }
}

function Assert-Payload {
    foreach ($file in Get-ChildItem -LiteralPath $stage -File -Recurse) {
        $relative = [IO.Path]::GetRelativePath($stage, $file.FullName)
        $installed = Join-Path $fixture $relative
        if (-not (Test-Path -LiteralPath $installed) -or
            (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash) {
            throw "Installed payload differs: $relative"
        }
    }
    if ((Get-FileHash -LiteralPath (Join-Path $bin 'obs64.exe') -Algorithm SHA256).Hash -ne $obsHash) { throw 'Installer changed OBS executable.' }
    if ((Get-Content -LiteralPath $sentinel -Raw) -ne 'Unrelated OBS plugin data') { throw 'Installer changed unrelated plugin.' }
}

Invoke-Installer (Join-Path $testRoot 'invalid-obs') 'invalid-root' $false
Invoke-Installer $fixture 'clean-install' $true
Assert-Payload
Invoke-Installer $fixture 'upgrade' $true
Assert-Payload

# This short-lived test fixture is ping.exe, deliberately named obs64.exe.
# It exits by itself; no real OBS instance is started, closed, or killed.
$processFixture = Join-Path $testRoot 'process-fixture'
New-Item -ItemType Directory -Path $processFixture | Out-Null
Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/ping.exe') -Destination (Join-Path $processFixture 'obs64.exe')
$fakeOBS = Start-Process -FilePath (Join-Path $processFixture 'obs64.exe') -ArgumentList @('-n', '12', '127.0.0.1') -WindowStyle Hidden -PassThru
try {
    Invoke-Installer $fixture 'obs-running-refusal' $false
    Assert-Payload
} finally {
    if (-not $fakeOBS.WaitForExit(20000)) { throw 'The self-terminating OBS process fixture did not exit.' }
}

$uninstaller = @(Get-ChildItem -LiteralPath (Join-Path $fixture 'data/obs-plugins/excelprotocol-fx/uninstall') -Filter 'unins*.exe')
if ($uninstaller.Count -ne 1) { throw 'Expected exactly one plugin uninstaller.' }
$uninstall = Start-Process -FilePath $uninstaller[0].FullName -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -WindowStyle Hidden -PassThru
if (-not $uninstall.WaitForExit(120000) -or $uninstall.ExitCode -ne 0) { throw 'Uninstall failed.' }
if (Test-Path -LiteralPath (Join-Path $fixture 'obs-plugins/64bit/excelprotocol-fx.dll')) { throw 'Uninstaller left plugin DLL behind.' }
if ((Get-FileHash -LiteralPath (Join-Path $bin 'obs64.exe') -Algorithm SHA256).Hash -ne $obsHash) { throw 'Uninstaller changed OBS.' }
if (-not (Test-Path -LiteralPath $sentinel)) { throw 'Uninstaller deleted unrelated plugin data.' }
Write-Host 'Installer smoke passed: invalid root, clean installation, upgrade, running OBS refusal, and uninstall preservation.'
