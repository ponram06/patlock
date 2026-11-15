# Runs on the HOST. Drives the Windows 11 test VM through VMware's vmrun so
# build/test/log steps can be automated. It never installs anything on the host.
# Drawing on the lock screen still needs a human: vmrun cannot send pointer input.
#
# One-time setup (host PowerShell; a throwaway VM admin account, never your real password):
#   [Environment]::SetEnvironmentVariable('PATLOCK_VM_USER', 'admin2', 'User')
#   [Environment]::SetEnvironmentVariable('PATLOCK_VM_PASS', '<that account password>', 'User')
# If the VM is encrypted (VMware requires that for a virtual TPM), also:
#   [Environment]::SetEnvironmentVariable('PATLOCK_VM_ENCRYPTION_PASS', '<VMware encryption password>', 'User')
# Then restart the terminal / VS Code.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Status', 'Screenshot', 'Update', 'Logs')][string]$Action,
    [string]$Vmx = "$env:USERPROFILE\Documents\Virtual Machines\Windows 11 x64 devop\Windows 11 x64 devop.vmx",
    [string]$GuestRepo = 'C:\patlock',
    [string]$OutDir = (Join-Path $env:TEMP 'patlock-vm')
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$vmrun = Join-Path ${env:ProgramFiles} 'VMware\VMware Workstation\vmrun.exe'
if (-not (Test-Path -LiteralPath $vmrun)) { $vmrun = Join-Path ${env:ProgramFiles(x86)} 'VMware\VMware Workstation\vmrun.exe' }
if (-not (Test-Path -LiteralPath $vmrun)) { throw 'vmrun.exe not found. Install VMware Workstation.' }
if (-not (Test-Path -LiteralPath $Vmx)) { throw "VM not found: $Vmx" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# Process environment first (one-off runs), then the saved user setting.
function Setting([string]$name) {
    $value = [Environment]::GetEnvironmentVariable($name, 'Process')
    if (-not $value) { $value = [Environment]::GetEnvironmentVariable($name, 'User') }
    return $value
}
$user = Setting 'PATLOCK_VM_USER'
$pass = Setting 'PATLOCK_VM_PASS'
$vmPass = Setting 'PATLOCK_VM_ENCRYPTION_PASS'
$vmAuth = @(); if ($vmPass) { $vmAuth = @('-vp', $vmPass) }
function Guest([string[]]$arguments) {
    if (-not $user -or -not $pass) { throw 'Set PATLOCK_VM_USER and PATLOCK_VM_PASS first (see the top of this script).' }
    & $vmrun -T ws @vmAuth -gu $user -gp $pass @arguments
    if ($LASTEXITCODE) { throw "vmrun $($arguments[0]) failed (exit $LASTEXITCODE)." }
}
function GuestPowerShell([string]$command, [string]$logName) {
    # Output goes to a file in the guest, then is copied back and printed here.
    $guestLog = "C:\Windows\Temp\$logName"
    $wrapped = "& { $command } *> '$guestLog'; exit `$LASTEXITCODE"
    $exitCode = 0
    try { Guest @('runProgramInGuest', $Vmx, '-activeWindow', 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', $wrapped) } catch { $exitCode = 1 }
    $hostLog = Join-Path $OutDir $logName
    Guest @('CopyFileFromGuestToHost', $Vmx, $guestLog, $hostLog)
    Get-Content -LiteralPath $hostLog
    if ($exitCode) { throw "Guest command failed; see $hostLog" }
}

switch ($Action) {
    'Status' {
        & $vmrun -T ws list
        if ($user -and $pass) { Guest @('checkToolsState', $Vmx) }
    }
    'Screenshot' {
        $file = Join-Path $OutDir ('screen-{0:yyyyMMdd-HHmmss}.png' -f (Get-Date))
        Guest @('captureScreen', $Vmx, $file)
        Write-Host $file
    }
    'Update' {
        GuestPowerShell "Set-Location -LiteralPath '$GuestRepo'; git pull --ff-only origin main; if (`$LASTEXITCODE) { exit 1 }; & '$GuestRepo\install\update.ps1' -AcknowledgeVm" 'patlock-update.log'
    }
    'Logs' {
        GuestPowerShell "Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Patlock'} -MaxEvents 50 -ErrorAction SilentlyContinue | Format-Table TimeCreated, Id, LevelDisplayName, Message -AutoSize -Wrap" 'patlock-events.log'
    }
}
