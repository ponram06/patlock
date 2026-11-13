Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-PatlockVm {
    param([switch]$AcknowledgeVm, [switch]$AllowNonAdmin)
    if (-not $AcknowledgeVm) { throw 'Specify -AcknowledgeVm only inside the test VM.' }
    if (-not [Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell.' }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        $principal = [Security.Principal.WindowsPrincipal]::new($identity)
        if (-not $AllowNonAdmin -and -not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            throw 'Run elevated inside the VM.'
        }
    } finally { $identity.Dispose() }
    $machine = Get-CimInstance Win32_ComputerSystem
    if ($machine.Manufacturer -notmatch 'VMware' -or $machine.Model -notmatch 'VMware') {
        throw 'This script only supports the VMware test VM. Host installation is prohibited.'
    }
}
