#!/usr/bin/env pwsh
#Requires -Version 5.1
# Bump vendored Ultimate ASI Loader to the latest upstream within the
# pinned v9.x range. Manual: dev runs this and commits the result. CI
# never refreshes. See ~/.claude/CLAUDE.md "Vendoring Third-Party Dependencies".

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$module = Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'
if (-not (Test-Path $module)) {
    throw "ModLoaderSetup.psm1 not found at $module. Run 'git submodule update --init' to pull cameraunlock-core."
}
Import-Module $module -Force

$vendorAsiDir = Join-Path $projectDir 'vendor/ultimate-asi-loader'
$vendorAsiDll = Join-Path $vendorAsiDir 'dinput8.dll'
$vendorLicense = Join-Path $vendorAsiDir 'LICENSE'
$vendorReadme  = Join-Path $vendorAsiDir 'README.md'
if (-not (Test-Path $vendorAsiDir)) {
    New-Item -ItemType Directory -Path $vendorAsiDir -Force | Out-Null
}

$tempZip = Join-Path $env:TEMP ("asi-update-" + [IO.Path]::GetRandomFileName() + ".zip")
$tempDll = Join-Path $env:TEMP ("asi-update-" + [IO.Path]::GetRandomFileName() + ".dll")
try {
    Write-Host "Refreshing vendor/ultimate-asi-loader from upstream..." -ForegroundColor Cyan
    $meta = Invoke-FetchLatestLoader `
        -OutputPath $tempZip `
        -Owner 'ThirteenAG' -Repo 'Ultimate-ASI-Loader' `
        -VersionPrefix 'v9.' `
        -AssetPattern '^Ultimate-ASI-Loader_x64\.zip$'

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($tempZip)
    try {
        $dllEntry = $zip.Entries | Where-Object { $_.Name -eq 'dinput8.dll' } | Select-Object -First 1
        if (-not $dllEntry) { throw "Upstream zip $($meta.AssetName) does not contain dinput8.dll." }
        $out = [System.IO.File]::Create($tempDll)
        try { $in = $dllEntry.Open(); try { $in.CopyTo($out) } finally { $in.Dispose() } } finally { $out.Dispose() }

        $dllSha = (Get-FileHash -LiteralPath $tempDll -Algorithm SHA256).Hash.ToLower()

        # Idempotency: an unchanged upstream must leave the tree clean. Without this
        # every run rewrites README.md with a fresh Fetched-at timestamp, so the diff
        # says "loader bumped" when nothing moved.
        if ((Test-Path $vendorAsiDll) -and (Test-Path $vendorLicense) -and (Test-Path $vendorReadme) -and
            ((Get-FileHash -LiteralPath $vendorAsiDll -Algorithm SHA256).Hash.ToLower() -eq $dllSha)) {
            Write-Host "  no change (tag=$($meta.Tag) sha256=$($dllSha.Substring(0,12))... matches on-disk vendor copy)" -ForegroundColor DarkGray
            return
        }

        Move-Item -LiteralPath $tempDll -Destination $vendorAsiDll -Force

        $licenseEntry = $zip.Entries | Where-Object { $_.Name -match '^(license|LICENSE)(\..+)?$' -and $_.FullName -notmatch '/.+/' } | Select-Object -First 1
        if ($licenseEntry) {
            $out = [System.IO.File]::Create($vendorLicense)
            try { $in = $licenseEntry.Open(); try { $in.CopyTo($out) } finally { $in.Dispose() } } finally { $out.Dispose() }
        }
    } finally { $zip.Dispose() }

    if (-not (Test-Path $vendorLicense)) {
        $licenseUrl = "https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/$($meta.Tag)/license"
        Invoke-WebRequest -Uri $licenseUrl -OutFile $vendorLicense -UseBasicParsing -TimeoutSec 30 -Headers @{ "User-Agent" = "CameraUnlock-HeadTracking" }
    }

    $readme = @(
        '# Ultimate ASI Loader (vendored)',
        '',
        'Bundled copy of Ultimate ASI Loader, the install-time source of truth.',
        'Refresh manually with `pixi run update-deps`, then commit.',
        '',
        '## Snapshot',
        '',
        '- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader',
        "- Tag: ``$($meta.Tag)``"
    )
    # The tag-to-commit lookup is best-effort upstream (unauthenticated GitHub API),
    # so a transient failure must drop the line rather than record an empty SHA over
    # the good one already committed.
    if ($meta.CommitSha) { $readme += "- Commit: ``$($meta.CommitSha)``" }
    $readme += @(
        "- Asset: ``$($meta.AssetName)``",
        "- dinput8.dll SHA-256: ``$dllSha``",
        "- Fetched at: $($meta.FetchedAt)",
        '',
        '`dinput8.dll` is extracted from the upstream asset untouched. install.cmd copies it to',
        "the Control exe directory as ``winmm.dll`` (Control's ASI hook slot)."
    )
    Set-Content -Path $vendorReadme -Value ($readme -join "`n") -Encoding UTF8

    Write-Host "  tag=$($meta.Tag) sha256=$($dllSha.Substring(0,12))..." -ForegroundColor DarkGray
} finally {
    Remove-Item $tempZip -Force -ErrorAction SilentlyContinue
    Remove-Item $tempDll -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "vendor/ultimate-asi-loader refreshed. Review and commit." -ForegroundColor Green
