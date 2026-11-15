# Fully removes Patlock and undoes everything the installer and enrollment created.
# Also available from Settings > Apps > Installed apps > Patlock. Reboot afterwards.
[CmdletBinding()]
param([switch]$KeepEnrollments, [switch]$Quiet)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Common.ps1"
Assert-Elevated -Script $MyInvocation.MyCommand.Path -Forward @($(if ($KeepEnrollments) {'-KeepEnrollments'}), $(if ($Quiet) {'-Quiet'}))
if (-not $Quiet) {
    $a = Read-Host 'This removes Patlock, all enrolled patterns and TPM keys. Windows password sign-in is unaffected. Type YES to continue'
    if ($a -ne 'YES') { return }
}
$dllPaths = @()
if (Test-Path "$ClassKey\InprocServer32") { $dllPaths += (Get-Item "$ClassKey\InprocServer32").GetValue('') }

Remove-PatlockRegistration
Remove-PatlockEventSource
if (-not $KeepEnrollments) {
    foreach ($line in (& certutil.exe -csp $Crypto -key 2>$null)) {
        $n = $line.Trim()
        if ($n -like 'Patlock_*') { & certutil.exe -csp $Crypto -delkey $n 2>&1 | Out-Null; Write-Host "Deleted TPM key $n" }
    }
    if (Test-Path $DataKey) { Remove-Item $DataKey -Recurse -Force; Write-Host "Removed $DataKey" }
}
# Legacy VM-test layout from earlier builds.
$dllPaths += (Join-Path $env:SystemRoot 'System32\PatlockProvider.dll'), (Join-Path $env:SystemRoot 'System32\PatlockSample.log')
foreach ($p in ($dllPaths | Where-Object { $_ } | Select-Object -Unique)) { Remove-FileOrSchedule $p }
Remove-Item -LiteralPath $ArpKey -Recurse -Force -ErrorAction SilentlyContinue
# Remove the install folder (this script lives in it, so finish with a detached cleanup).
if (Test-Path $InstallDir) {
    Get-ChildItem $InstallDir -Recurse -Force -File | Where-Object { $_.Name -notin 'Uninstall-Patlock.ps1','Common.ps1' } | ForEach-Object { Remove-FileOrSchedule $_.FullName }
    Start-Process cmd.exe -WindowStyle Hidden -ArgumentList '/c', "ping -n 3 127.0.0.1 >nul & rd /s /q `"$InstallDir`""
}
Write-Host 'Patlock removed. Reboot, then confirm Windows Password sign-in works.'
