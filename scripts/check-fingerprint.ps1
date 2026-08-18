# Prints the PE fingerprint of the installed game executables as a paste-ready
# build profile.
#
# This is the first thing to run when a user reports the "staying dormant"
# log line, and the first step of a rederive after a patch: it says whether the
# EXE actually changed at all. Asset-only patches move Steam's buildid and
# leave the executables untouched, in which case there is nothing to rederive.
[CmdletBinding()]
param([string]$GamePath)

$ErrorActionPreference = 'Stop'

if (-not $GamePath) {
    $module = Join-Path $PSScriptRoot '..\cameraunlock-core\powershell\GamePathDetection.psm1'
    if (Test-Path -LiteralPath $module) {
        Import-Module $module -Force
        $GamePath = Find-GamePath -GameId 'control-ultimate-edition'
    }
}
if (-not $GamePath -or -not (Test-Path -LiteralPath $GamePath)) {
    throw "Could not find Control. Pass the install folder: check-fingerprint.ps1 'D:\Games\Control'"
}

function Get-PeFingerprint([string]$path) {
    $fs = [System.IO.File]::OpenRead($path)
    try {
        $br = New-Object System.IO.BinaryReader($fs)
        $fs.Position = 0x3C
        $peOff = $br.ReadInt32()
        $fs.Position = $peOff + 8                # PE sig (4) + machine (2) + numSections (2)
        $timeDateStamp = $br.ReadUInt32()
        $fs.Position = $peOff + 4 + 20 + 56      # OptionalHeader.SizeOfImage
        $sizeOfImage = $br.ReadUInt32()
        $fs.Position = $peOff + 4 + 20 + 64      # OptionalHeader.CheckSum
        $checkSum = $br.ReadUInt32()
    } finally {
        $fs.Dispose()
    }
    [pscustomobject]@{
        TimeDateStamp = $timeDateStamp
        SizeOfImage   = $sizeOfImage
        CheckSum      = $checkSum
    }
}

$date = (Get-Date).ToString('yyyyMMdd')
foreach ($exe in @('Control_DX12.exe', 'Control_DX11.exe')) {
    $full = Join-Path $GamePath $exe
    if (-not (Test-Path -LiteralPath $full)) {
        Write-Warning "$exe not found in $GamePath"
        continue
    }
    $fp = Get-PeFingerprint $full
    $suffix = if ($exe -like '*DX12*') { 'Dx12' } else { 'Dx11' }
    $lower = $suffix.ToLower()

    Write-Output ""
    Write-Output "// $exe"
    Write-Output "static const BuildProfile kSteam${suffix}_${date} = {"
    Write-Output "    `"steam-win64-${lower}-${date}`","
    Write-Output "    `"$exe`","
    Write-Output "    {"
    Write-Output ("        0x{0:X8},  // TimeDateStamp" -f $fp.TimeDateStamp)
    Write-Output ("        0x{0:X},   // SizeOfImage" -f $fp.SizeOfImage)
    Write-Output ("        0x{0:X8},  // CheckSum" -f $fp.CheckSum)
    Write-Output "    },"
    Write-Output "    0x000000,  // cameraUpdateRva - REDERIVE THIS, do not copy it forward"
    Write-Output "};"
}

Write-Output ""
Write-Output "Compare against src/hooks/build_profile.cpp. If the fingerprints already"
Write-Output "match a profile there, the executables did not change and no rederive is needed."
