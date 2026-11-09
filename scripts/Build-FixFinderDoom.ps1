[CmdletBinding()]
param(
    [string]$Wad,
    [string]$EngineSource,
    [string]$ToolchainBin = "$env:LOCALAPPDATA/FixFinder/ArmGCC/bin",
    [switch]$Episode
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$revision = '25deefdb44427e068735d3ef2af1e9a44d5cac85'
$toolchain = (Resolve-Path -LiteralPath $ToolchainBin).Path
$build = Join-Path $root 'build'
if ($Wad) { $Wad = (Resolve-Path -LiteralPath $Wad).Path }
if ($EngineSource) {
    $EngineSource = (Resolve-Path -LiteralPath $EngineSource).Path
} else {
    $EngineSource = Join-Path $build 'GBADoom-pinned'
    if (!(Test-Path -LiteralPath $EngineSource)) {
        New-Item -ItemType Directory -Force -Path $build | Out-Null
        git clone --no-checkout https://github.com/doomhack/GBADoom.git $EngineSource
        if ($LASTEXITCODE) { throw 'Could not clone GBADoom.' }
        git -C $EngineSource checkout --detach $revision
        if ($LASTEXITCODE) { throw 'Could not check out the pinned GBADoom revision.' }
    }
}
$actual = git -C $EngineSource rev-parse HEAD
if ($LASTEXITCODE -or $actual -ne $revision) { throw "GBADoom must be checked out at $revision." }

$converter = Join-Path $EngineSource 'GbaWadUtil/GbaWadUtil.exe'
if (!$Wad) { $Wad = Join-Path $EngineSource 'GbaWadUtil/doom1.wad' }
$work = Join-Path $build "doom-ready-$([guid]::NewGuid())"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$reduced = Join-Path $work 'reduced.wad'
$converted = Join-Path $work 'converted.wad'
$prepare = Join-Path $PSScriptRoot 'Prepare-SharewareWad.py'
$maps = if ($Episode) { 9 } else { 1 }

python $prepare $Wad $reduced --maps $maps | Out-Host
if ($LASTEXITCODE) { throw 'Could not prepare the DOOM 1.9 shareware data.' }
Push-Location (Split-Path -Parent $converter)
try {
    & $converter -in $reduced -out $converted | Out-Host
    if ($LASTEXITCODE -or !(Test-Path -LiteralPath $converted -PathType Leaf)) {
        throw 'GbaWadUtil did not produce a converted WAD.'
    }
} finally {
    Pop-Location
}

$options = @{
    EngineSource = $EngineSource
    ConvertedWad = $converted
    ToolchainBin = $toolchain
    CompactReciprocal = $true
    FixFinderLcd = $true
    UsbGame = $true
    Arcade = $true
}
if ($Episode) {
    $compacted = Join-Path $work 'episode-compacted.wad'
    $assets = Join-Path $work 'episode-assets.wad'
    $pack = Join-Path $work 'FFDOOM1.PAK'
    python $prepare $converted $compacted --deduplicate-converted --compact-patches | Out-Host
    if ($LASTEXITCODE) { throw 'Could not compact the episode assets.' }
    python $prepare $compacted $assets --split-episode-maps $pack | Out-Host
    if ($LASTEXITCODE) { throw 'Could not split the episode map pack.' }
    $options.ConvertedWad = $assets
    $options.EmmcLibrary = $true
}

$elf = & "$PSScriptRoot/Build-DoomSwd.ps1" @options -PassThru
if (!(Test-Path -LiteralPath $elf -PathType Leaf)) { throw 'DOOM build did not produce an ELF.' }
$bin = [IO.Path]::ChangeExtension($elf, 'bin')
Copy-Item -LiteralPath $elf, $bin -Destination $work
Write-Output (Join-Path $work (Split-Path -Leaf $bin))
if ($Episode) { Write-Output $pack }
