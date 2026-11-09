[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$BoardSource,

    [switch]$SolidGreen,

    [string]$ToolchainBin = "$env:LOCALAPPDATA/FixFinder/ArmGCC/bin"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$board = (Resolve-Path -LiteralPath $BoardSource).Path
$toolchain = (Resolve-Path -LiteralPath $ToolchainBin).Path
$gcc = Join-Path $toolchain 'arm-none-eabi-gcc.exe'
$objcopy = Join-Path $toolchain 'arm-none-eabi-objcopy.exe'
$arch = @('-mcpu=cortex-m4', '-mthumb')
# probe-rs 0.32.0's GD32F427ZK definition uses a 16 KiB first sector.
$sectorSize = 16KB

function Invoke-Tool([string]$Path, [string[]]$Arguments) {
    & $Path @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$Path exited with code $LASTEXITCODE."
    }
}

function Write-FirstSectorImage([string]$ImagePath, [string]$SectorPath) {
    $image = [IO.File]::ReadAllBytes($ImagePath)
    if ($image.Length -gt $sectorSize) {
        throw "Image exceeds the first flash sector: $ImagePath"
    }
    $sector = [byte[]]::new($sectorSize)
    [Array]::Copy($image, $sector, $image.Length)
    for ($i = $image.Length; $i -lt $sectorSize; $i++) {
        $sector[$i] = 0xff
    }
    [IO.File]::WriteAllBytes($SectorPath, $sector)
}

$v8 = & "$PSScriptRoot/Build-V8.ps1" -ToolchainBin $toolchain -PassThru
$work = Join-Path $root "build/v9-$([guid]::NewGuid())"
New-Item -ItemType Directory -Path $work | Out-Null

Push-Location $root
try {
    $sdk = 'GD32F4xx_Firmware_Library/Firmware'
    $usb = "$sdk/GD32F4xx_usb_library"
    $flags = $arch + @(
        '-Os', '-ffunction-sections', '-fdata-sections', '-Wall', '-Wextra',
        '-DGD32F427', '-DUSE_USB_HS', '-DUSE_EMBEDDED_PHY', '-DHXTAL_VALUE=8000000U',
        '-DFIXFINDER_LCD_V9', '-Ifirmware/lcd', '-IFixFinder-USB-Test/inc',
        '-isystem', "$sdk/CMSIS", '-isystem', "$sdk/CMSIS/GD/GD32F4xx/Include",
        '-isystem', "$sdk/GD32F4xx_standard_peripheral/Include", '-isystem', "$usb/driver/Include",
        '-isystem', "$usb/device/core/Include", '-isystem', "$usb/device/class/hid/Include",
        '-isystem', "$usb/ustd/common", '-isystem', "$usb/ustd/class/hid"
    )
    if ($SolidGreen) {
        $flags += '-DFIXFINDER_LCD_SOLID_GREEN'
    }
    $v9Sources = @('USB-Build-V8/app.c', 'firmware/lcd/v9_main.c', 'firmware/lcd/hx8347.c',
        'firmware/lcd/v9_color_test.c', $board)
    $objects = @(Get-ChildItem -LiteralPath $v8 -Filter 'part_*.o' |
        Where-Object Name -ne 'part_1.o' | ForEach-Object FullName)
    for ($i = 0; $i -lt $v9Sources.Count; $i++) {
        $object = Join-Path $work "v9_$i.o"
        Invoke-Tool $gcc ($flags + @('-c', $v9Sources[$i], '-o', $object))
        $objects += $object
    }

    $elf = Join-Path $work 'FixFinder-LCD-V9.elf'
    $bin = Join-Path $work 'FixFinder-LCD-V9.bin'
    $map = Join-Path $work 'FixFinder-LCD-V9.map'
    Invoke-Tool $gcc ($arch + @('-nostartfiles', "-Wl,--defsym=__fixfinder_flash_size=$sectorSize",
        '-T', 'USB-Build-V8/firmware.ld',
        '-Wl,--gc-sections,--print-memory-usage', "-Wl,-Map,$map", '-o', $elf) + $objects)
    Invoke-Tool $objcopy @('-O', 'binary', $elf, $bin)
    $candidateSector = Join-Path $work 'FixFinder-LCD-V9-sector0.bin'
    $recoverySector = Join-Path $work 'FixFinder-USB-V8-sector0.bin'
    Write-FirstSectorImage $bin $candidateSector
    Write-FirstSectorImage (Join-Path $v8 'FixFinder-USB-V8.bin') $recoverySector
    foreach ($sector in $candidateSector, $recoverySector) {
        $hash = (Get-FileHash -LiteralPath $sector).Hash
        Write-Output "$hash  $sector"
    }
    Write-Output "Linked V9 diagnostic ELF at $elf"
} finally {
    Pop-Location
}
