param(
    [Parameter(Position = 0)]
    [string]$Version,
    # Ship a release even when there are no user-facing commits since the
    # last tag (writes a maintenance changelog entry instead of aborting).
    [switch]$Force
)

$ErrorActionPreference = "Stop"

# THIRD-PARTY-NOTICES.md names the cameraunlock-core commit compiled into the
# release ZIPs, and bumping the submodule does not touch it. Packaging refuses
# to ship that mismatch, so a bump with no notices edit stopped the release
# here, or in CI once the tag had already been pushed. Re-sync it and let this
# release carry the correction.
$noticesRoot = Split-Path -Parent $PSScriptRoot
& git -C $noticesRoot diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) { throw "THIRD-PARTY-NOTICES.md has uncommitted edits. Commit or discard them, then re-run." }
& (Join-Path $noticesRoot 'cameraunlock-core\scripts\sync-core-notices.ps1') -Repo $noticesRoot
if ($LASTEXITCODE -ne 0) { throw "sync-core-notices.ps1 exited $LASTEXITCODE - fix THIRD-PARTY-NOTICES.md before releasing." }
& git -C $noticesRoot diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) {
    & git -C $noticesRoot commit -q -m 'chore: record the cameraunlock-core commit this build compiles' -- THIRD-PARTY-NOTICES.md
    if ($LASTEXITCODE -ne 0) { throw "Could not commit the re-synced THIRD-PARTY-NOTICES.md." }
    Write-Host 'THIRD-PARTY-NOTICES.md re-synced to the pinned cameraunlock-core commit.' -ForegroundColor Yellow
}

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Import-Module (Join-Path $ProjectRoot "cameraunlock-core\powershell\ReleaseWorkflow.psm1") -Force

# Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry
# lands in the same place with the same shape.
function Add-MaintenanceChangelogEntry {
    param([string]$Path, [string]$NewVersion)
    $date = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"
    $changelog = Get-Content $Path -Raw
    if ($changelog -match '(?s)(# Changelog.*?)(## \[)') {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$entry"
    } else {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n)', "`$1$entry"
    }
    $changelog = $changelog.TrimEnd() + "`n"
    Set-Content $Path $changelog -NoNewline
}

if (-not $Version) {
    Write-Error "Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>"
    exit 1
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1')
    exit $LASTEXITCODE
}

$constantsFile  = Join-Path $ProjectRoot "src\core\constants.h"
$cmakeFile      = Join-Path $ProjectRoot "CMakeLists.txt"
$changelogFile  = Join-Path $ProjectRoot "CHANGELOG.md"
$manifestFile   = Join-Path $ProjectRoot "launcher-manifest.json"
$installCmdFile = Join-Path $ProjectRoot "scripts\install.cmd"

$currentVersionMatch = Select-String -Path $constantsFile -Pattern 'CONTROLHT_VERSION\s*=\s*"([^"]+)"'
if (-not $currentVersionMatch) {
    Write-Error "Could not read current version from constants.h"
    exit 1
}
$currentVersion = $currentVersionMatch.Matches[0].Groups[1].Value

$Version = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $currentVersion
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    Write-Error "Resolved version '$Version' is not semantic X.Y.Z"
    exit 1
}

Write-Host "Releasing v$Version (from v$currentVersion)..." -ForegroundColor Cyan

Push-Location $ProjectRoot
try {
    $branch = (git rev-parse --abbrev-ref HEAD).Trim()
    if ($branch -ne 'main') {
        Write-Error "Releases must run from 'main' (current: '$branch')"
        exit 1
    }
    if (-not (Test-CleanGitStatus)) {
        Write-Error "Working tree is dirty. Commit or stash before releasing."
        exit 1
    }
    if (Test-GitTagExists -Tag "v$Version") {
        Write-Error "Tag v$Version already exists."
        exit 1
    }
} finally {
    Pop-Location
}

# Generate CHANGELOG from commits since last tag. This is the gate that
# aborts when there are no user-facing commits, so run it BEFORE mutating
# any version files - a failure here then leaves a clean tree instead of
# stranding a half-applied version bump with no tag.
Write-Host "Generating CHANGELOG entry..." -ForegroundColor Cyan
$hasExistingTags = git tag -l 2>$null
if (-not $hasExistingTags) {
    if (-not (Test-Path $changelogFile)) {
        $date = Get-Date -Format 'yyyy-MM-dd'
        "# Changelog`n`n## [$Version] - $date`n`nFirst release.`n" | Set-Content $changelogFile
        Write-Host "  Wrote initial CHANGELOG.md" -ForegroundColor Gray
    }
} else {
    try {
        $null = New-ChangelogFromCommits -ChangelogPath $changelogFile -Version $Version
    } catch {
        if (-not $Force) {
            Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host "No user-facing changes to release. Re-run with -Force for a maintenance release." -ForegroundColor Yellow
            exit 1
        }
        Write-Host "No user-facing commits since last tag - writing maintenance entry (-Force)." -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path $changelogFile -NewVersion $Version
    }
}

$content = Get-Content $constantsFile -Raw
$content = $content -replace '(CONTROLHT_VERSION\s*=\s*")([^"]+)(")', "`${1}$Version`${3}"
Set-Content -Path $constantsFile -Value $content -NoNewline
Write-Host "  Updated constants.h -> $Version" -ForegroundColor Green

$cmakeContent = Get-Content $cmakeFile -Raw
$cmakeContent = $cmakeContent -replace '(project\(ControlHeadTracking VERSION )\d+\.\d+\.\d+', "`${1}$Version"
Set-Content -Path $cmakeFile -Value $cmakeContent -NoNewline
Write-Host "  Updated CMakeLists.txt -> $Version" -ForegroundColor Green

# The packager stamps the shipped manifest from the build, so the ZIP is
# correct either way - this keeps the in-repo copy from drifting a version
# behind everything else after every release.
$manifestContent = Get-Content $manifestFile -Raw
$manifestContent = $manifestContent -replace '("version":\s*")\d+\.\d+\.\d+(")', "`${1}$Version`${2}"
Set-Content -Path $manifestFile -Value $manifestContent -NoNewline
Write-Host "  Updated launcher-manifest.json -> $Version" -ForegroundColor Green

# install.cmd's MOD_VERSION is what the install writes into the launcher's
# state file, which is where the launcher looks to spot a stale install.
$installCmdContent = Get-Content $installCmdFile -Raw
if ($installCmdContent -notmatch 'set "MOD_VERSION=[^"]+"') { throw "MOD_VERSION line not found in $installCmdFile" }
$installCmdContent = $installCmdContent -replace 'set "MOD_VERSION=[^"]+"', "set `"MOD_VERSION=$Version`""
Set-Content -Path $installCmdFile -Value $installCmdContent -NoNewline
Write-Host "  Updated install.cmd -> $Version" -ForegroundColor Green

Write-Host "Building release..." -ForegroundColor Cyan
& pixi run build
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed"; exit 1 }

Write-Host "Packaging..." -ForegroundColor Cyan
& (Join-Path $PSScriptRoot "package-release.ps1")
if ($LASTEXITCODE -ne 0) { Write-Error "Packaging failed"; exit 1 }

Push-Location $ProjectRoot
try {
    Write-Host "Committing version bump..." -ForegroundColor Cyan
    git add $constantsFile $cmakeFile $manifestFile $installCmdFile $changelogFile
    git commit -m "Release v$Version"
    if ($LASTEXITCODE -ne 0) { Write-Error "Commit failed"; exit 1 }

    Write-Host "Creating annotated tag v$Version..." -ForegroundColor Cyan
    New-ReleaseTag -Version $Version -Message "Release v$Version" -Branch 'main'
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "Released v$Version. CI will publish artifacts from the v$Version tag." -ForegroundColor Green
