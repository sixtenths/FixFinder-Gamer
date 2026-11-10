[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Arcade,
    [Parameter(Mandatory)][string]$Sonic,
    [string]$ToolchainBin = "$env:LOCALAPPDATA/FixFinder/ArmGCC/bin"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$arcadeImage = (Resolve-Path -LiteralPath $Arcade).Path
$sonicImage = (Resolve-Path -LiteralPath $Sonic).Path
$work = Join-Path $root "build/game-switch-$([guid]::NewGuid())"
New-Item -ItemType Directory -Path $work | Out-Null
foreach ($game in @('arcade', 'sonic')) {
    $image = if ($game -eq 'arcade') { $arcadeImage } else { $sonicImage }
    $package = Join-Path $work "$game.pkg"
    python -B "$PSScriptRoot/Prepare-SwitchPackage.py" $game $image $package
    if ($LASTEXITCODE) { throw "Could not package $game." }

    $source = Join-Path $work "$game-seed.S"
    $path = $package.Replace('\', '/')
    @"
.section .rodata.switch_seed,"a",%progbits
.balign 4
.global switch_seed_start, switch_seed_end
switch_seed_start:
.incbin "$path"
switch_seed_end:
"@ | Set-Content -LiteralPath $source -Encoding ascii
    $sources = @(Join-Path $root 'firmware/switcher/seed.c'; $source)
    $out = & "$PSScriptRoot/Build-UsbLoader.ps1" -ToolchainBin $ToolchainBin -GameSdk -SwitchSeed -GameSource $sources
    Copy-Item -LiteralPath (Join-Path $out 'FixFinder-Game.bin') -Destination (Join-Path $work "$game-seed.bin")
}
$out = & "$PSScriptRoot/Build-UsbLoader.ps1" -ToolchainBin $ToolchainBin -SwitchSupervisor
Copy-Item -LiteralPath (Join-Path $out 'FixFinder-Switcher-USB.bin') -Destination $work
Write-Output $work
