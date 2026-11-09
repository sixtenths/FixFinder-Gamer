[CmdletBinding()]
param(
    [string]$ToolchainBin = "$env:LOCALAPPDATA/FixFinder/ArmGCC/bin",
    [string]$GameSource,
    [ValidateSet('UART3', 'USART0')]
    [string]$RadioUart,
    [switch]$RamTest
)

$ErrorActionPreference = 'Stop'
if ($RamTest -and $GameSource) { throw 'Select -RamTest or -GameSource.' }
if ($RadioUart -and $GameSource) { throw '-RadioUart applies to the resident loader.' }
$toolchain = (Resolve-Path -LiteralPath $ToolchainBin).Path
if ($GameSource) { $GameSource = (Resolve-Path -LiteralPath $GameSource).Path }
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root "build/usb-loader-$([guid]::NewGuid())"
$arch = @('-mcpu=cortex-m4', '-mthumb')

function Invoke-Gcc([string[]]$Arguments) {
    & (Join-Path $toolchain 'arm-none-eabi-gcc.exe') @arch @Arguments | Out-Host
    if ($LASTEXITCODE) { throw "arm-none-eabi-gcc exited with code $LASTEXITCODE" }
}

function Compile([string]$Source, [string]$Name, [string[]]$Defines) {
    $object = Join-Path $work "$Name.o"
    Invoke-Gcc ($compile + $Defines + @('-c', $Source, '-o', $object))
    $object
}

function Export-Image([string]$Name, [string]$Linker, [string[]]$Layout, [string[]]$Objects) {
    $elf = Join-Path $work "$Name.elf"
    Invoke-Gcc ($Layout + @('-nostartfiles', '-T', $Linker,
        '-Wl,--gc-sections', '-o', $elf) + $Objects)
    & (Join-Path $toolchain 'arm-none-eabi-objcopy.exe') -O binary $elf (Join-Path $work "$Name.bin")
    if ($LASTEXITCODE) { throw "Could not export $Name.bin." }
}

$v8 = & "$PSScriptRoot/Build-V8.ps1" -ToolchainBin $toolchain -PassThru -LoaderObjects:(!$GameSource)
New-Item -ItemType Directory -Path $work | Out-Null
Push-Location $root
try {
    $sdk = 'GD32F4xx_Firmware_Library/Firmware'
    $peripheral = "$sdk/GD32F4xx_standard_peripheral"
    $usb = "$sdk/GD32F4xx_usb_library"
    $compile = @('-Os', '-ffunction-sections', '-fdata-sections', '-DGD32F427',
        '-DUSE_USB_HS', '-DUSE_EMBEDDED_PHY', '-DHXTAL_VALUE=8000000U',
        '-IFixFinder-USB-Test/inc', "-I$sdk/CMSIS", "-I$sdk/CMSIS/GD/GD32F4xx/Include",
        "-I$peripheral/Include", "-I$usb/driver/Include", "-I$usb/device/core/Include",
        "-I$usb/device/class/hid/Include", "-I$usb/ustd/common", "-I$usb/ustd/class/hid")
    $common = @(Get-ChildItem -LiteralPath $v8 -Filter 'part_*.o' |
        Where-Object Name -notin @('part_1.o', 'part_22.o') | ForEach-Object FullName)

    if ($GameSource) {
        $defines = @('-DFIXFINDER_USB_GAME')
        $objects = $common + @(
            (Compile 'USB-Build-V8/app.c' 'game-entry' $defines),
            (Compile 'USB-Build-V8/custom_hid_core.c' 'game-hid' $defines),
            (Compile $GameSource 'game' $defines))
        $layout = @('-Wl,--defsym,__fixfinder_app_base=0x08008000')
        Export-Image 'FixFinder-Game' 'firmware/doom-swd/firmware.ld' $layout $objects
    } else {
        $defines = @('-DFIXFINDER_USB_LOADER', '-Oz', '-flto')
        $layout = @('-Oz', '-flto')
        $name = 'FixFinder-USB-Loader'
        if ($RamTest) {
            $defines += '-DFIXFINDER_LOADER_RAM_TEST'
            $layout += '-Wl,--defsym,__ram_test=1'
            $name += '-RAM'
        }
        if ($RadioUart) {
            $defines += '-DFIXFINDER_RADIO', "-DFIXFINDER_RADIO_$RadioUart"
            $name += "-$RadioUart"
        }
        $objects = $common + @(
            (Compile 'firmware/usb-loader/loader.c' 'loader' $defines),
            (Compile 'firmware/usb-loader/screen.c' 'loader-screen' $defines),
            (Compile 'USB-Build-V8/custom_hid_core.c' 'loader-hid' $defines),
            (Compile "$peripheral/Source/gd32f4xx_fmc.c" 'fmc' $defines))
        if ($RadioUart) {
            $objects += Compile 'firmware/usb-loader/radio_uart.c' 'radio-uart' $defines
        }
        Export-Image $name 'firmware/usb-loader/loader.ld' $layout $objects
    }
    Write-Output $work
} finally {
    Pop-Location
}
