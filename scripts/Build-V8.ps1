[CmdletBinding()]
param(
    [string]$ToolchainBin = "$env:LOCALAPPDATA/FixFinder/ArmGCC/bin"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$toolchain = (Resolve-Path -LiteralPath $ToolchainBin).Path
$gcc = Join-Path $toolchain 'arm-none-eabi-gcc.exe'
$objcopy = Join-Path $toolchain 'arm-none-eabi-objcopy.exe'
$build = Join-Path $root 'build'
$expected = '271E301E4F6271FEB6980DFFDB21C0530EB7B11F3951C5EBB8425E7F5C1D3B0C'

function Invoke-Tool([string]$Path, [string[]]$Arguments) {
    & $Path @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$Path exited with code $LASTEXITCODE"
    }
}

Push-Location $root
try {
    $baseline = 'USB-Build-V8/FixFinder-USB-V8.bin'
    if ((Get-FileHash -LiteralPath $baseline).Hash -ne $expected) {
        throw 'The preserved V8 recovery image failed its SHA-256 check.'
    }

    $sdk = 'GD32F4xx_Firmware_Library/Firmware'
    $cmsis = "$sdk/CMSIS"
    $device = "$cmsis/GD/GD32F4xx"
    $peripheral = "$sdk/GD32F4xx_standard_peripheral"
    $usb = "$sdk/GD32F4xx_usb_library"
    $original = 'FixFinder-USB-Test/src'
    $v8 = 'USB-Build-V8'

    $includes = @(
        'FixFinder-USB-Test/inc', $cmsis, "$device/Include",
        "$peripheral/Include", "$usb/driver/Include", "$usb/device/core/Include",
        "$usb/device/class/hid/Include", "$usb/ustd/common", "$usb/ustd/class/hid"
    )
    $sources = @(
        "$v8/gd32f4xx_it.c", "$v8/app.c", "$original/custom_hid_itf.c",
        "$original/gd32f4xx_hw.c", "$v8/system_gd32f4xx.c"
    )
    $sources += @(
        'exti', 'gpio', 'misc', 'pmu', 'rcu', 'syscfg', 'timer', 'usart', 'dma', 'spi', 'ctc'
    ) | ForEach-Object { "$peripheral/Source/gd32f4xx_$_.c" }
    $sources += @(
        "$usb/driver/Source/drv_usb_core.c", "$usb/driver/Source/drv_usb_dev.c",
        "$usb/driver/Source/drv_usbd_int.c", "$usb/device/core/Source/usbd_core.c",
        "$usb/device/core/Source/usbd_enum.c", "$usb/device/core/Source/usbd_transc.c",
        "$v8/custom_hid_core.c",
        "$device/Source/GCC/startup_gd32f407_427.S", 'USB-Build/crt_stubs.c'
    )

    $arch = @('-mcpu=cortex-m4', '-mthumb')
    $compile = $arch + @(
        '-Os', '-ffunction-sections', '-fdata-sections',
        '-DGD32F427', '-DUSE_USB_HS', '-DUSE_EMBEDDED_PHY', '-DHXTAL_VALUE=8000000U'
    )
    $compile += $includes | ForEach-Object { "-I$_" }

    New-Item -ItemType Directory -Force -Path $build | Out-Null
    $work = Join-Path $build ("v8-" + [guid]::NewGuid())
    New-Item -ItemType Directory -Path $work | Out-Null
    $objects = for ($i = 0; $i -lt $sources.Count; $i++) {
        $object = Join-Path $work "part_$i.o"
        Invoke-Tool $gcc ($compile + @('-c', $sources[$i], '-o', $object))
        $object
    }

    $elf = Join-Path $work 'FixFinder-USB-V8.elf'
    $bin = Join-Path $work 'FixFinder-USB-V8.bin'
    $map = Join-Path $work 'FixFinder-USB-V8.map'
    Invoke-Tool $gcc ($arch + @('-nostartfiles', '-T', "$v8/firmware.ld",
        '-Wl,--gc-sections', "-Wl,-Map,$map", '-o', $elf) + $objects)
    Invoke-Tool $objcopy @('-O', 'binary', $elf, $bin)

    $actual = (Get-FileHash -LiteralPath $bin).Hash
    if ($actual -ne $expected) {
        throw "V8 build differs from recovery image (SHA-256 $actual). Inspect $work."
    }
    Write-Output "V8 reproduced byte for byte ($actual). Output: $work"
} finally {
    Pop-Location
}
