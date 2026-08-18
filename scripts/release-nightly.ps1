[CmdletBinding()]
param(
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

$constantsFile = Join-Path $ProjectRoot 'src\core\constants.h'
$versionMatch = Select-String -Path $constantsFile -Pattern 'CONTROLHT_VERSION\s*=\s*"([^"]+)"'
if (-not $versionMatch) {
    throw "Could not extract version from $constantsFile"
}
$version = $versionMatch.Matches[0].Groups[1].Value

Publish-NightlyBuild `
    -ModId 'control-ultimate-edition' `
    -ModName 'ControlHeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -BuildCommand 'pixi run build' `
    -AllowDirty:$AllowDirty
