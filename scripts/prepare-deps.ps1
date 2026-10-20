[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$fxRoot = Split-Path $PSScriptRoot -Parent
$fxDeps = Join-Path $fxRoot '.deps'
New-Item -ItemType Directory -Path $fxDeps -Force | Out-Null
function Get-VerifiedArchive([string]$Name, [string]$Url, [string]$Sha256, [string]$Destination) {
    $archive = Join-Path $fxDeps $Name
    if (!(Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $Url -OutFile $archive }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Sha256) {
        throw "SHA-256 mismatch: $Name. Remove this archive and retry."
    }
    $marker = Join-Path $Destination '.fx-verified-sha256'
    if (!(Test-Path -LiteralPath $marker) -or (Get-Content $marker -Raw).Trim() -ne $Sha256) {
        # Archives are pinned official OBS distribution assets.
        Expand-Archive -LiteralPath $archive -DestinationPath $Destination -Force
        Set-Content -LiteralPath $marker -Value $Sha256 -Encoding ascii
    }
}
Get-VerifiedArchive 'qt6.zip' 'https://github.com/obsproject/obs-deps/releases/download/2026-07-15/windows-deps-qt6-2026-07-15-x64.zip' '7c7f985711d80467bdc1795b6592275a27d5b0e5a2c7a61db1f2c1d08d6a5579' (Join-Path $fxDeps 'qt6')
Get-VerifiedArchive 'obs-runtime.zip' 'https://github.com/obsproject/obs-studio/releases/download/32.2.2/OBS-Studio-32.2.2-Windows-x64.zip' '4d6e40e3ab155f56b30de517380566a206d74b63cdf5ad49aa596924768f97e1' (Join-Path $fxDeps 'obs-runtime')
$fxSource = Join-Path $fxDeps 'obs-studio'
if (!(Test-Path -LiteralPath (Join-Path $fxSource '.git'))) {
    git clone --depth 1 --branch 32.2.2 https://github.com/obsproject/obs-studio.git $fxSource
    if ($LASTEXITCODE) { throw 'OBS source download failed' }
}
$fxCommit = git -C $fxSource rev-parse HEAD
if ($LASTEXITCODE -or $fxCommit.Trim() -ne 'ba2f32bdf791005443988a4955e963663e16b1ed') { throw 'Unexpected OBS SDK source commit' }
$fxDirty = git -C $fxSource status --porcelain --untracked-files=no
if ($fxDirty) { throw 'OBS SDK source has local modifications' }
$fxSdk = Join-Path $fxDeps 'obs-sdk'
New-Item -ItemType Directory -Path $fxSdk -Force | Out-Null
@'
#pragma once
#define OBS_DATA_PATH "data"
#define OBS_PLUGIN_PATH "obs-plugins/64bit"
#define OBS_PLUGIN_DESTINATION "obs-plugins"
#define OBS_INSTALL_PREFIX ""
#define OBS_RELEASE_CANDIDATE 0
#define OBS_BETA 0
'@ | Set-Content -LiteralPath (Join-Path $fxSdk 'obsconfig.h') -Encoding ascii
foreach ($fxLibrary in @('obs', 'obs-frontend-api')) {
    $dll = Join-Path $fxDeps "obs-runtime/bin/64bit/$fxLibrary.dll"
    $exports = & dumpbin.exe /NOLOGO /EXPORTS $dll
    if ($LASTEXITCODE) { throw "dumpbin failed: $fxLibrary. Use an x64 MSVC developer shell." }
    $names = @($exports | ForEach-Object {
        if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)') { $Matches[1] }
    })
    if ($names.Count -lt 20) { throw "Cannot read exports from $dll" }
    $definition = Join-Path $fxSdk "$fxLibrary.def"
    @("LIBRARY $fxLibrary.dll", 'EXPORTS') + $names | Set-Content -LiteralPath $definition -Encoding ascii
    & lib.exe /NOLOGO /MACHINE:X64 "/DEF:$definition" "/OUT:$(Join-Path $fxSdk "$fxLibrary.lib")"
    if ($LASTEXITCODE) { throw "Import library generation failed: $fxLibrary" }
}
Write-Host 'OBS 32.2.2 and Qt 6.11.1 dependencies ready.'
