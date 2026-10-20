[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$Repository,
    [Parameter(Mandatory)][string]$Commit,
    [string]$StagePath = 'stage',
    [string]$OutputPath = 'dist',
    [string]$Python = 'python',
    [string]$IsccPath = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
$stage = (Resolve-Path -LiteralPath $StagePath).Path
$output = [IO.Path]::GetFullPath((Join-Path (Get-Location) $OutputPath))
$metadata = Join-Path $root 'build-packaging'
New-Item -ItemType Directory -Path $output, $metadata -Force | Out-Null
if (-not (Test-Path -LiteralPath $IsccPath -PathType Leaf)) { throw 'Install Inno Setup 6 or pass -IsccPath to its ISCC.exe.' }
& $Python (Join-Path $PSScriptRoot 'release_tools.py') version --version $Version
if ($LASTEXITCODE -ne 0) { throw 'Invalid release version.' }
& $Python (Join-Path $PSScriptRoot 'release_tools.py') payload --stage $stage --output $metadata
if ($LASTEXITCODE -ne 0) { throw 'Installation payload validation failed.' }
$numericVersion = ($Version -replace '^v', '') -replace '-beta\.\d+$', ''
$releaseVersion = $Version -replace '^v', ''
& $IsccPath "/DAppVersion=$releaseVersion" "/DVersionNumber=$numericVersion.0" "/DStageDir=$stage" "/DMetadataDir=$metadata" "/DOutputDir=$output" (Join-Path $root 'installer\ExcelProtocol-FX.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
& $Python (Join-Path $PSScriptRoot 'release_tools.py') package --stage $stage --output $output --version $releaseVersion --repository $Repository --commit $Commit
if ($LASTEXITCODE -ne 0) { throw 'Release packaging failed.' }
Write-Host "Validated release bundle: $output"
