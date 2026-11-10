[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Rom,
    [string]$GwenesisSource,
    [string]$ToolchainBin = "$env:LOCALAPPDATA/FixFinder/ArmGCC/bin"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$revision = '168e46661a086a0b793a9ee127a206604ebe7df1'
if (!$GwenesisSource) {
    $GwenesisSource = Join-Path $root 'build/gwenesis-pinned'
    if (!(Test-Path -LiteralPath $GwenesisSource)) {
        git clone --no-checkout https://github.com/bzhxx/gwenesis.git $GwenesisSource
        if ($LASTEXITCODE) { throw 'Gwenesis clone failed.' }
        git -C $GwenesisSource checkout --detach $revision
        if ($LASTEXITCODE) { throw 'Could not check out pinned Gwenesis.' }
    }
}
$actual = git -C $GwenesisSource rev-parse HEAD
if ($LASTEXITCODE -or $actual -ne $revision) { throw "Use Gwenesis revision $revision." }
git -C $GwenesisSource diff --quiet HEAD -- src
if ($LASTEXITCODE) { throw 'Use unmodified Gwenesis source.' }

$staged = Join-Path $root "build/genesis-src-$([guid]::NewGuid())"
$sources = @(python -B "$PSScriptRoot/Prepare-Gwenesis.py" $GwenesisSource $romPath $staged)
if ($LASTEXITCODE) { throw 'Could not stage Genesis sources and cartridge.' }
$sources += Join-Path $root 'firmware/genesis/sonic.c'
$includes = @('bus', 'cpus/M68K', 'cpus/Z80', 'io', 'sound', 'savestate', 'vdp') |
    ForEach-Object { Join-Path $staged $_ }
$out = & "$PSScriptRoot/Build-UsbLoader.ps1" -ToolchainBin $ToolchainBin -GameSdk -GameSource $sources `
    -GameInclude $includes -GameHeapKiB 0
$bin = Join-Path $out 'FixFinder-Game.bin'
$target = Join-Path $out 'FixFinder-Sonic-USB.bin'
Copy-Item -LiteralPath $bin -Destination $target
Write-Output $target
