# Run ONLY inside the Windows 11 VMware VM.
[CmdletBinding()]
param([switch]$AcknowledgeVm)
. "$PSScriptRoot/VmGuard.ps1"
Assert-PatlockVm -AcknowledgeVm:$AcknowledgeVm
$clsid = '{21d42e91-a8c0-4e63-b5d7-372d8585982c}'
$providerKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$clsid"
$classKey = "HKLM:\SOFTWARE\Classes\CLSID\$clsid"
$expectedDll = Join-Path $env:SystemRoot 'System32/PatlockProvider.dll'
$marker = 'Patlock Pattern Provider'
if (Test-Path -LiteralPath $classKey) {
    $registration = Get-Item -LiteralPath $classKey
    $server = Get-Item -LiteralPath "$classKey\InprocServer32"
    if ($registration.GetValue('') -ne $marker -or $server.GetValue('') -ne $expectedDll) {
        throw 'CLSID ownership does not match this installation; refusing to remove it.'
    }
}
if (Test-Path -LiteralPath $providerKey) {
    if ((Get-Item -LiteralPath $providerKey).GetValue('') -ne $marker) {
        throw 'Provider ownership does not match this installation; refusing to remove it.'
    }
}
if (Test-Path -LiteralPath $providerKey) { Remove-Item -LiteralPath $providerKey -Recurse -Force }
if (Test-Path -LiteralPath $classKey) { Remove-Item -LiteralPath $classKey -Recurse -Force }
Write-Host 'Only the sample registration was removed. Reboot, verify Password login, then remove System32/PatlockProvider.dll inside the VM if no longer needed.'
