# Build and automated tests ONLY. No elevation, registration, enrollment or reboot.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
try {
    & "$PSScriptRoot/build-and-test.ps1" -AcknowledgeVm
    Write-Host 'Automated tests finished. No sign-in configuration was changed.' -ForegroundColor Green
} catch {
    Write-Host ('STOPPED: ' + $_.Exception.Message) -ForegroundColor Red
    Write-Host 'No provider installation or uninstallation was attempted.'
    exit 1
}
