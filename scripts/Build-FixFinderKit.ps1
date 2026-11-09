[CmdletBinding()]
param(
    [string]$ToolchainBin = "$env:LOCALAPPDATA/FixFinder/ArmGCC/bin"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$revision = git -C $root rev-parse --short HEAD
if ($LASTEXITCODE) { throw 'Build the kit from a Git checkout.' }
$changes = git -C $root status --porcelain --untracked-files=no
if ($LASTEXITCODE -or $changes) { throw 'Build the kit from a clean checkout so its binaries match its source.' }

$build = Join-Path $root 'build'
$stage = Join-Path $build "kit-stage-$([guid]::NewGuid())"
New-Item -ItemType Directory -Force -Path $stage | Out-Null
$source = Join-Path $stage 'source.zip'
$bundle = Join-Path $stage 'FixFinder-Gamer'
git -C $root archive --format=zip -o $source HEAD
if ($LASTEXITCODE) { throw 'Could not archive the source checkout.' }
Expand-Archive -LiteralPath $source -DestinationPath $bundle
$bin = Join-Path $bundle 'firmware-bin'
New-Item -ItemType Directory -Path $bin | Out-Null

$builder = Join-Path $PSScriptRoot 'Build-UsbLoader.ps1'
$loader = & $builder -ToolchainBin $ToolchainBin
$arcade = & $builder -ToolchainBin $ToolchainBin -ArcadeOnly
$example = Join-Path $root 'examples/pixel-catcher/main.c'
$demo = & $builder -ToolchainBin $ToolchainBin -GameSdk -GameSource $example
Copy-Item -LiteralPath (Join-Path $loader 'FixFinder-USB-Loader.bin') -Destination $bin
Copy-Item -LiteralPath (Join-Path $arcade 'FixFinder-Arcade.bin') -Destination $bin
Copy-Item -LiteralPath (Join-Path $demo 'FixFinder-Game.bin') -Destination (Join-Path $bin 'Pixel-Catcher.bin')
Get-ChildItem -LiteralPath $bin -Filter '*.bin' | Sort-Object Name | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant(), $_.Name
} | Set-Content -LiteralPath (Join-Path $bin 'SHA256SUMS') -Encoding ascii

$name = "FixFinder-Gamer-Kit-$revision.zip"
$zip = Join-Path $build $name
Compress-Archive -LiteralPath $bundle -DestinationPath $zip -Force
'{0}  {1}' -f (Get-FileHash -LiteralPath $zip).Hash.ToLowerInvariant(), $name |
    Set-Content -LiteralPath "$zip.sha256" -Encoding ascii
Write-Output $zip
