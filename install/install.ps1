# Run ONLY inside the Windows 11 VMware VM after taking a snapshot.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DllPath,
    [switch]$AcknowledgeVm,
    [switch]$SnapshotAndPasswordFallbackVerified
)
. "$PSScriptRoot/VmGuard.ps1"
Assert-PatlockVm -AcknowledgeVm:$AcknowledgeVm
if (-not $SnapshotAndPasswordFallbackVerified) {
    throw 'Take a snapshot and verify a second administrator Password login, then pass -SnapshotAndPasswordFallbackVerified.'
}
$source = (Resolve-Path -LiteralPath $DllPath).Path
$destination = Join-Path $env:SystemRoot 'System32/PatlockProvider.dll'
$clsid = '{21d42e91-a8c0-4e63-b5d7-372d8585982c}'
$providerKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$clsid"
$classKey = "HKLM:\SOFTWARE\Classes\CLSID\$clsid"
if ((Test-Path -LiteralPath $destination) -or (Test-Path -LiteralPath $providerKey) -or (Test-Path -LiteralPath $classKey)) {
    throw 'Sample already installed or identity occupied. Uninstall and reboot before updating; no files are overwritten.'
}
# Inspect the PE header without loading the DLL into this process.
$stream = [IO.File]::OpenRead($source)
$reader = [IO.BinaryReader]::new($stream)
try {
    if ($reader.ReadUInt16() -ne 0x5A4D) { throw 'Not a PE file.' }
    $stream.Position = 0x3c
    $peOffset = $reader.ReadInt32()
    if ($peOffset -lt 0x40 -or $peOffset -gt ($stream.Length - 24)) { throw 'Invalid PE header.' }
    $stream.Position = $peOffset
    if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x8664) {
        throw 'Expected an x64 Windows DLL.'
    }
    $stream.Position = $peOffset + 22
    if (($reader.ReadUInt16() -band 0x2000) -eq 0) { throw 'The PE file is not a DLL.' }
} finally { $reader.Dispose() }
$copied = $false
try {
    Copy-Item -LiteralPath $source -Destination $destination -ErrorAction Stop
    $copied = $true
    $logPath = Join-Path $env:SystemRoot 'System32/PatlockSample.log'
    if (-not (Test-Path -LiteralPath $logPath)) {
        New-Item -Path $logPath -ItemType File | Out-Null
    }
    New-Item -Path "$classKey\InprocServer32" -Force | Out-Null
    Set-Item -LiteralPath $classKey -Value 'Patlock Pattern Provider'
    Set-Item -LiteralPath "$classKey\InprocServer32" -Value $destination
    New-ItemProperty -LiteralPath "$classKey\InprocServer32" -Name ThreadingModel -Value Apartment -PropertyType String | Out-Null
    # Register the tile last. No provider filter or built-in provider is touched.
    New-Item -Path $providerKey -Force | Out-Null
    Set-Item -LiteralPath $providerKey -Value 'Patlock Pattern Provider'
} catch {
    # Disable enumeration first if registration was incomplete.
    Remove-Item -LiteralPath $providerKey -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $classKey -Recurse -Force -ErrorAction SilentlyContinue
    if ($copied) {
        Remove-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $destination) {
            Write-Warning 'Registration failed; reboot the VM and remove System32/PatlockProvider.dll before retrying.'
        }
    }
    throw
}
Write-Host 'Sample registered in VM. Test Password fallback before testing the sample. Reboot if it is not enumerated.'
