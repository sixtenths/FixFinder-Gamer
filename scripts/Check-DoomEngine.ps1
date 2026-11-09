[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$EngineSource,

    [string]$ToolchainBin = "$env:LOCALAPPDATA/FixFinder/ArmGCC/bin"
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $EngineSource).Path
$toolchain = (Resolve-Path -LiteralPath $ToolchainBin).Path
$expectedRevision = '25deefdb44427e068735d3ef2af1e9a44d5cac85'
$revision = & git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $revision -ne $expectedRevision) {
    throw "Check out GBADoom revision $expectedRevision first."
}
& git -C $source diff --quiet HEAD -- include source
if ($LASTEXITCODE -ne 0) {
    throw 'Use the unmodified GBADoom sources for this check.'
}

function Invoke-ArmTool([string]$Name, [string[]]$Arguments) {
    & (Join-Path $toolchain "arm-none-eabi-$Name.exe") @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "arm-none-eabi-$Name exited with code $LASTEXITCODE."
    }
}

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build/doom-engine-$([guid]::NewGuid())"
New-Item -ItemType Directory -Path $build -Force | Out-Null
$arch = @('-mcpu=cortex-m4', '-mthumb')
$flags = $arch + @(
    '-Os', '-ffunction-sections', '-fdata-sections', '-fno-common',
    '-DHAVE_CONFIG_H', '-std=gnu23', '-include', 'stdint.h', "-I$source/include",
    '-Wno-incompatible-pointer-types', '-Wno-implicit-function-declaration'
)
# The platform entry, audio driver and WAD are supplied by the eventual firmware.
$files = Get-ChildItem -LiteralPath (Join-Path $source 'source') -File -Filter '*.c' |
    Where-Object Name -notin 'i_audio.c', 'i_main.c', 'doom_iwad.c'
$objects = foreach ($file in $files) {
    $object = Join-Path $build ($file.BaseName + '.o')
    Invoke-ArmTool gcc ($flags + @('-c', $file.FullName, '-o', $object)) | Out-Host
    $object
}

$engine = Join-Path $build 'engine.o'
Invoke-ArmTool gcc ($arch + @('-r', '-nostdlib') + $objects + @('-o', $engine))
Write-Output "Cross-compiled $($objects.Count) engine sources to $engine."
Invoke-ArmTool size @($engine)
Invoke-ArmTool nm @('-u', $engine)
