# Production installer for Patlock. Run from the release folder (right-click > Run with PowerShell).
[CmdletBinding()]
param([switch]$AllowUnsigned, [switch]$SkipRestorePoint, [switch]$Elevated)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Common.ps1"
Assert-Elevated -Script $MyInvocation.MyCommand.Path -Forward @(
    $(if ($AllowUnsigned) {'-AllowUnsigned'}), $(if ($SkipRestorePoint) {'-SkipRestorePoint'}), '-Elevated') | Out-Null
if (-not [Environment]::Is64BitOperatingSystem -or -not [Environment]::Is64BitProcess) { throw 'Patlock needs 64-bit Windows and 64-bit PowerShell.' }
if ([Environment]::OSVersion.Version.Major -lt 10) { throw 'Windows 10 or later is required.' }

$root = $PSScriptRoot
if (-not (Test-Path (Join-Path $root 'PatlockProvider.dll'))) { $root = Split-Path -Parent $PSScriptRoot }
$dllSrc = Join-Path $root 'PatlockProvider.dll'; $enrollSrc = Join-Path $root 'patlock_enroll.exe'
foreach ($f in @($dllSrc, $enrollSrc)) { if (-not (Test-Path -LiteralPath $f)) { throw "Missing $f" } }

# Integrity: SHA256SUMS must match, and the DLL should be signed (it runs inside LogonUI as SYSTEM).
$sums = Join-Path $root 'SHA256SUMS.txt'
if (-not (Test-Path -LiteralPath $sums)) { throw 'SHA256SUMS.txt is required for installation.' }
$verified = @{}
foreach ($line in Get-Content -LiteralPath $sums) {
    if ($line -match '^\s*$') { continue }
        $h, $n = $line -split '\s+', 2
        if (-not $h -or -not $n -or -not (Test-Path -LiteralPath (Join-Path $root $n.Trim()))) { throw "Invalid SHA256SUMS entry: $line" }
        if ((Get-FileHash (Join-Path $root $n.Trim()) -Algorithm SHA256).Hash -ne $h) { throw "Hash mismatch for $n. Do not install." }
        $verified[$n.Trim()] = $true
}
if (-not $verified['PatlockProvider.dll'] -or -not $verified['patlock_enroll.exe']) { throw 'SHA256SUMS.txt must contain both Patlock binaries.' }
$sig = Get-AuthenticodeSignature -LiteralPath $dllSrc
if ($sig.Status -ne 'Valid' -and -not $AllowUnsigned) { throw "PatlockProvider.dll is not validly signed ($($sig.Status)). Use -AllowUnsigned only for testing." }

if (-not $SkipRestorePoint) {
    try { Enable-ComputerRestore -Drive "$env:SystemDrive\" -ErrorAction Stop; Checkpoint-Computer -Description 'Before Patlock install' -RestorePointType MODIFY_SETTINGS -ErrorAction Stop }
    catch { Write-Warning "Restore point skipped: $($_.Exception.Message)" }
}

$version = (Get-Item $dllSrc).VersionInfo.FileVersion; if (-not $version) { $version = '1.0.0' }
$short = (Get-FileHash $dllSrc -Algorithm SHA256).Hash.Substring(0, 8).ToLower()
$bin = Join-Path $InstallDir 'bin'
$dllDst = Join-Path $bin "PatlockProvider.$short.dll"   # unique name: upgrades never collide with a DLL LogonUI has loaded
$created = $false
try {
    New-Item -ItemType Directory -Path $bin -Force | Out-Null; $created = $true
    Copy-Item $dllSrc $dllDst -Force
    Copy-Item $enrollSrc (Join-Path $InstallDir 'patlock_enroll.exe') -Force
    foreach ($f in 'Common.ps1','Uninstall-Patlock.ps1') { Copy-Item (Join-Path $PSScriptRoot $f) $InstallDir -Force }
    foreach ($d in 'RECOVERY.md','README.md') { if (Test-Path (Join-Path $root $d)) { Copy-Item (Join-Path $root $d) $InstallDir -Force } }
    # Only SYSTEM/Administrators may write; everyone may read/execute.
    & icacls.exe $InstallDir /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-32-545:(OI)(CI)RX' | Out-Null
    if ($LASTEXITCODE) { throw 'Failed to set permissions on the install folder.' }

    $old = if (Test-Path $ClassKey) { (Get-Item "$ClassKey\InprocServer32").GetValue('') } else { $null }
    Remove-PatlockRegistration   # upgrade path: unregister the previous build first
    New-Item -Path "$ClassKey\InprocServer32" -Force | Out-Null
    Set-Item -LiteralPath $ClassKey -Value $Marker
    Set-Item -LiteralPath "$ClassKey\InprocServer32" -Value $dllDst
    New-ItemProperty -LiteralPath "$ClassKey\InprocServer32" -Name ThreadingModel -Value Apartment -PropertyType String -Force | Out-Null
    New-Item -Path $ProvKey -Force | Out-Null            # tile registered last
    Set-Item -LiteralPath $ProvKey -Value $Marker
    Register-PatlockEventSource -MessageFile $dllDst

    @{ version = $version; dll = $dllDst; installed = (Get-Date).ToString('o') } | ConvertTo-Json | Set-Content (Join-Path $InstallDir 'install.json') -Encoding ascii
    New-Item -Path $ArpKey -Force | Out-Null
    $props = @{ DisplayName = 'Patlock'; DisplayVersion = $version; Publisher = 'Patlock'; InstallLocation = $InstallDir
        UninstallString = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$InstallDir\Uninstall-Patlock.ps1`"" }
    foreach ($k in $props.Keys) { New-ItemProperty -LiteralPath $ArpKey -Name $k -Value $props[$k] -PropertyType String -Force | Out-Null }
    New-ItemProperty -LiteralPath $ArpKey -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
    # Remove the previous generation's DLL (delayed to reboot if LogonUI still has it open).
    if ($old -and $old -ne $dllDst -and $old -like "$bin\*") { Remove-FileOrSchedule $old }
} catch {
    Remove-PatlockRegistration
    Remove-PatlockEventSource
    Remove-Item -LiteralPath $ArpKey -Recurse -Force -ErrorAction SilentlyContinue
    if ($created) { Remove-Item -LiteralPath $InstallDir -Recurse -Force -ErrorAction SilentlyContinue }
    throw
}
Write-Host "Patlock $version installed. Run `"$InstallDir\patlock_enroll.exe`" to set your pattern, then lock (Win+L) to test. Password sign-in stays available."
