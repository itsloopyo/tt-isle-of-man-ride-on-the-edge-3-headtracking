[CmdletBinding()]
param(
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\ReleaseWorkflow.psm1') -Force
Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

$version = Get-ProjectVersion -Source pixi -Path (Join-Path $ProjectRoot 'pixi.toml')

# package-release.ps1 builds only the installer ZIP, no Nexus layout.
Publish-NightlyBuild `
    -ModId 'tt-isle-of-man-ride-on-the-edge-3' `
    -ModName 'TT3HeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -BuildCommand 'pixi run build' `
    -NoNexusZip `
    -AllowDirty:$AllowDirty
