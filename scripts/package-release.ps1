#!/usr/bin/env pwsh
#Requires -Version 5.1
# Package release ZIPs for Control Head Tracking.
#   - ControlHeadTracking-v<version>-installer.zip: install.cmd + uninstall.cmd
#     + plugins/ControlHeadTracking.asi + vendor/ultimate-asi-loader/ + docs
#     + licenses/.
#   - ControlHeadTracking-v<version>-nexus.zip: ControlHeadTracking.asi plus
#     LICENSE + THIRD-PARTY-NOTICES.md + licenses/ (extract-to-game-exe-folder
#     layout, Nexus users manage their own loader).

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectDir "cameraunlock-core\powershell\ReleaseWorkflow.psm1") -Force

# Read version from CMakeLists.txt project() declaration.
$cmakeText = Get-Content (Join-Path $projectDir "CMakeLists.txt") -Raw
if ($cmakeText -notmatch 'project\(ControlHeadTracking\s+VERSION\s+(\d+\.\d+\.\d+)') {
    throw "Could not parse VERSION from CMakeLists.txt"
}
$version = $Matches[1]

Write-Host "=== Control Head Tracking - Package Release ===" -ForegroundColor Magenta
Write-Host "Version: $version" -ForegroundColor Cyan

$releaseDir = Join-Path $projectDir "release"
if (-not (Test-Path $releaseDir)) { New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null }

$asi = Join-Path $projectDir "bin/Release/ControlHeadTracking.asi"
if (-not (Test-Path $asi)) { throw "Build artifact missing: $asi (run 'pixi run build')" }

$vendorAsiDll = Join-Path $projectDir "vendor/ultimate-asi-loader/dinput8.dll"
if (-not (Test-Path $vendorAsiDll)) {
    throw "vendor/ultimate-asi-loader/dinput8.dll missing. Run 'pixi run update-deps' to populate the vendor tree before packaging."
}

# Verbatim third-party license texts for everything compiled into the .asi.
# THIRD-PARTY-NOTICES.md names each component, but BSD-2-Clause wants the
# conditions and the disclaimer themselves and MIT wants its own notice, so the
# upstream files travel too.
# Missing is fatal rather than skipped: a release that quietly drops these is
# one that redistributes BSD code without its license.
function Copy-VendoredLicenses {
    param([Parameter(Mandatory)][string]$StagingDir)

    $licenses = @{
        # MinHook, and the Hacker Disassembler Engine it carries in src/hde -
        # one upstream file covers both.
        "minhook-LICENSE.txt" = Join-Path $projectDir "extern/minhook/LICENSE.txt"
        # CameraUnlock Core is MIT under a different copyright holder from this
        # mod's own LICENSE, and MIT wants its notice in every copy including a
        # binary one, so it travels as its own file rather than being assumed
        # covered by ours.
        "cameraunlock-core-LICENSE.txt" = Join-Path $projectDir "cameraunlock-core/LICENSE"
    }

    $outDir = Join-Path $StagingDir "licenses"
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
    foreach ($name in $licenses.Keys) {
        $src = $licenses[$name]
        if (-not (Test-Path $src)) {
            throw "Third-party license missing: $src. It is compiled into the .asi, so it must ship with it."
        }
        Copy-Item $src -Destination (Join-Path $outDir $name) -Force
    }
    Write-Host "Staged: licenses/ ($($licenses.Count) third-party license text(s))" -ForegroundColor Green
}

$modManifestPath = Join-Path $projectDir "launcher-manifest.json"
if (-not (Test-Path $modManifestPath)) {
    throw "launcher-manifest.json not found at project root. The launcher manifest must ship in the installer ZIP."
}

# --- Installer ZIP ---
$ghStaging = Join-Path $releaseDir "staging-installer"
if (Test-Path $ghStaging) { Remove-Item -Recurse -Force $ghStaging }
New-Item -ItemType Directory -Path $ghStaging -Force | Out-Null

# install.cmd carries MOD_VERSION into the .headtracking-state.json the
# launcher reads for update checks, so it is stamped from the build the same
# way the manifest is. Copying it verbatim would pin every install to whatever
# the working copy happened to say, which after the first release is 0.0.0
# forever. Written back as CRLF, which .cmd files require.
$installCmdText = Get-Content (Join-Path $scriptDir "install.cmd") -Raw
$installCmdText = $installCmdText -replace '(set "MOD_VERSION=)\d+\.\d+\.\d+(")', "`${1}$version`$2"
$installCmdText = $installCmdText -replace "`r?`n", "`r`n"
[System.IO.File]::WriteAllText((Join-Path $ghStaging "install.cmd"), $installCmdText,
    (New-Object System.Text.UTF8Encoding $false))
Copy-Item (Join-Path $scriptDir "uninstall.cmd") -Destination $ghStaging -Force

# Shared shim so install.cmd and uninstall.cmd can resolve the game path at
# install time. Copy-SharedBundle is the only sanctioned way to stage it: it
# carries the whole set the .cmd scripts reach for (find-game.ps1,
# GamePathDetection.psm1, games.json, the loader-arch and marker checks) and
# prints the core commit the bundle came from. Hand-copying a subset is how a
# release ships a .cmd that hard-errors on a file nobody noticed was missing.
Write-Host "Staging shared bundle..." -ForegroundColor Cyan
Copy-SharedBundle -StagingDir $ghStaging

$pluginsDir = Join-Path $ghStaging "plugins"
New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
Copy-Item $asi -Destination $pluginsDir -Force

$vendorOut = Join-Path $ghStaging "vendor/ultimate-asi-loader"
New-Item -ItemType Directory -Path $vendorOut -Force | Out-Null
Copy-Item $vendorAsiDll -Destination $vendorOut -Force
# The loader binary is redistributed here, and MIT requires its notice to be in
# every copy, so a missing LICENSE is fatal rather than skipped - the same rule
# Copy-VendoredLicenses applies to what is compiled into the .asi. README.md is
# only provenance for us, so it is copied when present.
$vendorLicense = Join-Path $projectDir "vendor/ultimate-asi-loader/LICENSE"
if (-not (Test-Path $vendorLicense)) {
    throw "vendor/ultimate-asi-loader/LICENSE missing. dinput8.dll is redistributed in this ZIP and its MIT notice has to ship with it."
}
Copy-Item $vendorLicense -Destination $vendorOut -Force
$vendorReadme = Join-Path $projectDir "vendor/ultimate-asi-loader/README.md"
if (Test-Path $vendorReadme) { Copy-Item $vendorReadme -Destination $vendorOut -Force }

foreach ($doc in @("README.md", "LICENSE", "CHANGELOG.md", "THIRD-PARTY-NOTICES.md")) {
    $src = Join-Path $projectDir $doc
    if (Test-Path $src) { Copy-Item $src -Destination $ghStaging -Force }
}

Copy-VendoredLicenses -StagingDir $ghStaging

# Canonical launcher manifest. The launcher reads launcher-manifest.json (and
# only that file) from the installer ZIP root to detect, install, and track the
# package. Stamp the version from the build so the shipped manifest can never
# disagree with the built .asi.
$stagedManifest = Join-Path $ghStaging "launcher-manifest.json"
$manifestText = Get-Content $modManifestPath -Raw
$manifestText = $manifestText -replace '("version":\s*")\d+\.\d+\.\d+(")', "`${1}$version`$2"
[System.IO.File]::WriteAllText($stagedManifest, $manifestText, (New-Object System.Text.UTF8Encoding $false))
Write-Host "Staged: launcher-manifest.json (version $version)" -ForegroundColor Green

$installerZip = Join-Path $releaseDir "ControlHeadTracking-v$version-installer.zip"
if (Test-Path $installerZip) { Remove-Item $installerZip -Force }
Compress-Archive -Path (Join-Path $ghStaging "*") -DestinationPath $installerZip -Force
Write-Host "Created: $installerZip" -ForegroundColor Green

# --- Nexus ZIP (the .asi + notices, extract-to-game-folder) ---
# The .asi statically links MinHook and, inside it, the Hacker Disassembler
# Engine. Both are BSD-2-Clause, whose second condition requires the copyright
# notice, the list of conditions AND the disclaimer to be reproduced in the
# materials accompanying a binary redistribution - so the verbatim upstream
# license text ships here, not just a summary of it. Nexus users manage their
# own loader and need none of the rest of the installer tree.
$nexusStaging = Join-Path $releaseDir "staging-nexus"
if (Test-Path $nexusStaging) { Remove-Item -Recurse -Force $nexusStaging }
New-Item -ItemType Directory -Path $nexusStaging -Force | Out-Null
Copy-Item $asi -Destination $nexusStaging -Force
foreach ($doc in @("LICENSE", "THIRD-PARTY-NOTICES.md")) {
    Copy-Item (Join-Path $projectDir $doc) -Destination $nexusStaging -Force
}

Copy-VendoredLicenses -StagingDir $nexusStaging

$nexusZip = Join-Path $releaseDir "ControlHeadTracking-v$version-nexus.zip"
if (Test-Path $nexusZip) { Remove-Item $nexusZip -Force }
# The Nexus ZIP is a binary distribution too: the licences of everything
# compiled into or bundled with the payload require their notices to travel
# with it, so LICENSE and THIRD-PARTY-NOTICES.md ship at its root.
foreach ($noticeDoc in @('LICENSE', 'THIRD-PARTY-NOTICES.md', 'README.md')) {
    $noticeSrc = Join-Path $projectDir $noticeDoc
    if (-not (Test-Path $noticeSrc)) {
        throw "Required notice file not found: $noticeDoc. Every published ZIP is a binary distribution and must carry it."
    }
    Copy-Item $noticeSrc -Destination $nexusStaging -Force
    Write-Host "  $noticeDoc" -ForegroundColor Green
}
Compress-Archive -Path (Join-Path $nexusStaging "*") -DestinationPath $nexusZip -Force
Write-Host "Created: $nexusZip" -ForegroundColor Green

Remove-Item -Recurse -Force $ghStaging
Remove-Item -Recurse -Force $nexusStaging

Write-Host ""
Write-Host "Done." -ForegroundColor Green
