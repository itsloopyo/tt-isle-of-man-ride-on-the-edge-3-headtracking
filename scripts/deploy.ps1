#!/usr/bin/env pwsh
#Requires -Version 5.1
# Dev loop: copy the built .asi and the vendored ASI loader into every install
# of the game on this machine. A -GamePath argument replaces detection and is
# the only target.

[CmdletBinding()]
param(
    # Positional so `deploy.ps1 "D:\Games\TT3"` works, matching the positional
    # game path install.cmd takes.
    [Parameter(Position = 0)][string]$GamePath,
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot

Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/DevDeploy.psm1')

# TT3.exe statically imports both DINPUT8.dll and VERSION.dll, and neither is a
# KnownDLL, so a game-local copy of either wins the DLL search order.
# VERSION.dll is the proxy because DINPUT8.dll carries the input API the game
# actually calls.
Invoke-DevDeployASILoader `
    -GameId 'tt-isle-of-man-ride-on-the-edge-3' `
    -GameDisplayName 'TT Isle of Man: Ride on the Edge 3' `
    -BuildOutputPath (Join-Path $projectDir "build/$Config") `
    -ModDllName 'TT3HeadTracking.asi' `
    -VendorLoaderDll (Join-Path $projectDir 'vendor/ultimate-asi-loader/dinput8.dll') `
    -AsiLoaderName 'version.dll' `
    -GivenPath $GamePath | Out-Null
