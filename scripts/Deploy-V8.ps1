[CmdletBinding(DefaultParameterSetName = 'Stage')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Inspect')]
    [switch]$Inspect,

    [Parameter(Mandatory, ParameterSetName = 'Restore')]
    [switch]$Restore,

    [Parameter(Mandatory, ParameterSetName = 'Restore')]
    [ValidatePattern('^[0-9A-Fa-f]{8} [0-9A-Fa-f]{8} [0-9A-Fa-f]{8}$')]
    [string]$ExpectedUid,

    [ValidatePattern('^[A-Za-z0-9_][A-Za-z0-9_-]*@[A-Za-z0-9][A-Za-z0-9.-]*$')]
    [string]$Pi = 'zac@pi'
)

$ErrorActionPreference = 'Stop'
$sshOptions = @('-o', 'BatchMode=yes', '-o', 'StrictHostKeyChecking=yes', '-o', 'ConnectTimeout=8')
$probe = 'sudo -n /home/zac/.cargo/bin/probe-rs'
$target = "--chip GD32F427ZK --protocol swd --probe '0:0:gpiochip0,swclk=25,swdio=24' --speed 50"

function Invoke-Pi([string]$Command) {
    $output = & ssh @sshOptions $Pi $Command
    if ($LASTEXITCODE -ne 0) {
        throw "SSH or remote command failed with exit code $LASTEXITCODE."
    }
    return ($output -join "`n").Trim()
}

if ($Inspect) {
    $cpuId = Invoke-Pi "$probe read b32 0xE000ED00 1 --format simple-hex $target"
    if ($cpuId -ne '410fc241') {
        throw "Unexpected CPUID: $cpuId"
    }
    $uid = Invoke-Pi "$probe read b32 0x1FFF7A10 3 --format simple-hex $target"
    if ($uid -notmatch '^[0-9a-f]{8} [0-9a-f]{8} [0-9a-f]{8}$') {
        throw "Unexpected UID output: $uid"
    }
    $vectors = Invoke-Pi "$probe read b32 0x08000000 2 --format simple-hex $target"
    "CPUID: $cpuId"
    "UID (in address order): $uid"
    "Initial SP and reset vector: $vectors"
    return
}

$bin = Join-Path (Split-Path -Parent $PSScriptRoot) 'USB-Build-V8/FixFinder-USB-V8.bin'
$expected = '271e301e4f6271feb6980dffdb21c0530eb7b11f3951c5ebb8425e7f5c1d3b0c'
if ((Get-FileHash -LiteralPath $bin).Hash -ne $expected) {
    throw 'The local V8 recovery image failed its SHA-256 check.'
}
$dir = Invoke-Pi 'mktemp -d /tmp/fixfinder-v8.XXXXXXXX'
if ($dir -cnotmatch '^/tmp/fixfinder-v8\.[A-Za-z0-9]{8}$') {
    throw "Unexpected staging directory: $dir"
}
$remote = "$dir/FixFinder-USB-V8.bin"
& scp @sshOptions $bin "${Pi}:$remote"
if ($LASTEXITCODE -ne 0) {
    throw "SCP failed with exit code $LASTEXITCODE."
}
$check = "echo '$expected  $remote' | sha256sum -c -"
Invoke-Pi $check

if ($Restore) {
    $uid = $ExpectedUid.ToLowerInvariant()
    # Recheck the staged image and target on the Pi immediately before downloading.
    $command = "$check && cpu=`$($probe read b32 0xE000ED00 1 --format simple-hex $target) && " +
        'case $cpu in 410fc241) ;; *) echo ''Unexpected CPUID'' >&2; exit 1 ;; esac && ' +
        "uid=`$($probe read b32 0x1FFF7A10 3 --format simple-hex $target) && " +
        "case `$uid in '$uid') ;; *) echo 'Unexpected target UID' >&2; exit 1 ;; esac && " +
        "$probe download $remote $target --binary-format bin --base-address 0x08000000 --verify"
    Invoke-Pi $command
    'V8 programmed and verified on the experimental scanner.'
} else {
    "Verified V8 staged at ${Pi}:$remote"
}
