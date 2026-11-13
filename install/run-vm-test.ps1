# Source-only on the development host. Double-click RUN_VM_TEST.cmd IN THE VM.
[CmdletBinding()]
param([switch]$Elevated)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

try {
    # Check VMware BEFORE elevation or any mutations.
    if (-not [Environment]::Is64BitProcess) { throw 'Use the 64-bit launcher.' }
    $machine = Get-CimInstance Win32_ComputerSystem
    if ($machine.Manufacturer -notmatch 'VMware' -or $machine.Model -notmatch 'VMware') {
        throw 'VM ONLY: this launcher refuses to run on your main PC.'
    }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        $admin = ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)
    } finally { $identity.Dispose() }
    if (-not $admin) {
        $arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -Elevated' -f $PSCommandPath
        $child = Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -Verb RunAs -ArgumentList $arguments -Wait -PassThru
        exit $child.ExitCode
    }
    . "$PSScriptRoot/VmGuard.ps1"
    Assert-PatlockVm -AcknowledgeVm
    $repoRoot = (Resolve-Path -LiteralPath "$PSScriptRoot/..").Path
    Set-Location -LiteralPath $repoRoot
    $checkpointPath = Join-Path $repoRoot '.patlock-vm-test.json'
    $dll = Join-Path $repoRoot 'build/Release/PatlockProvider.dll'
    $enrollPath = Join-Path $repoRoot 'build/Release/patlock_enroll.exe'
    $demoPath = Join-Path $repoRoot 'build/Release/pattern_window_demo.exe'
    $installedDll = Join-Path $env:SystemRoot 'System32/PatlockProvider.dll'
    $boot = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')
    Write-Host 'Patlock VM test: keep a snapshot and working Password fallback.' -ForegroundColor Cyan
    Write-Host 'No password or pattern is requested on this console. Enter secrets only in the enrollment dialog.'

    if (-not (Test-Path -LiteralPath $checkpointPath)) {
        if ((Read-Host 'Snapshot taken AND backup administrator Password login verified? Type READY') -cne 'READY') {
            throw 'Snapshot and Password fallback confirmation required.'
        }
        # Use VS2022 bundled CMake if it is not already on PATH. CMake's VS
        # generator discovers MSBuild/SDK without a Developer PowerShell session.
        if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
            $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
            if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install VS2022 Desktop development with C++ and CMake tools in the VM.' }
            $vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'VS2022 C++ tools not found.' }
            $cmakeBin = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
            if (-not (Test-Path -LiteralPath (Join-Path $cmakeBin 'cmake.exe'))) { throw 'Install C++ CMake tools for Windows in the VM.' }
            $env:PATH = $cmakeBin + [IO.Path]::PathSeparator + $env:PATH
        }
        if (-not (Get-Command ctest -ErrorAction SilentlyContinue)) { throw 'ctest was not found alongside CMake.' }
        & "$PSScriptRoot/build-and-test.ps1" -AcknowledgeVm
        # Script throws on any failed configure/build/test. Never uninstall on failure.
        $hash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
        # Uninstall checks ownership before touching the project's registration.
        & "$PSScriptRoot/uninstall.ps1" -AcknowledgeVm
        [ordered]@{ Version = 1; Stage = 'AwaitingReboot'; Boot = $boot; DllHash = $hash
            EnrollHash = (Get-FileHash -LiteralPath $enrollPath -Algorithm SHA256).Hash
            DemoHash = (Get-FileHash -LiteralPath $demoPath -Algorithm SHA256).Hash } |
            ConvertTo-Json | Set-Content -LiteralPath $checkpointPath -Encoding UTF8
        Write-Host 'Build/tests passed and old registration removed. A reboot is required.' -ForegroundColor Green
        Write-Host 'After reboot: sign in using Password, then double-click RUN_VM_TEST.cmd again.'
        if ((Read-Host 'Save other VM work. Type RESTART to reboot now; Enter to reboot later') -ceq 'RESTART') { Restart-Computer }
        exit 0
    }

    $state = Get-Content -LiteralPath $checkpointPath -Raw | ConvertFrom-Json
    if ($state.Version -ne 1 -or $state.Stage -notin @('AwaitingReboot', 'Installed') -or
        $state.DllHash -notmatch '^[0-9A-Fa-f]{64}$') { throw 'Invalid checkpoint; do not continue installation. Ask for help.' }
    if ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -ne $state.DllHash) {
        throw 'Build output changed since testing. Remove only .patlock-vm-test.json and rerun the launcher to rebuild/test.'
    }
    if ((Get-FileHash -LiteralPath $enrollPath -Algorithm SHA256).Hash -ne $state.EnrollHash -or
        (Get-FileHash -LiteralPath $demoPath -Algorithm SHA256).Hash -ne $state.DemoHash) {
        throw 'Enrollment/demo output changed since testing. Restart the workflow after removing only its checkpoint.'
    }
    if ($state.Stage -eq 'AwaitingReboot') {
        if ($state.Boot -eq $boot) { throw 'Reboot the VM, sign in with Password and run this launcher again.' }
        if ((Read-Host 'Did Password sign-in work after reboot? Type PASSWORD-OK') -cne 'PASSWORD-OK') {
            throw 'Verify Password fallback before installing.'
        }
        # Delete only the known old DLL after confirmed reboot/unregistration.
        $clsid = '{21d42e91-a8c0-4e63-b5d7-372d8585982c}'
        if ((Test-Path -LiteralPath "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$clsid") -or
            (Test-Path -LiteralPath "HKLM:\SOFTWARE\Classes\CLSID\$clsid")) { throw 'Patlock is still registered. Stop and check recovery instructions.' }
        if (Test-Path -LiteralPath $installedDll) { Remove-Item -LiteralPath $installedDll -Force -ErrorAction Stop }
        & "$PSScriptRoot/install.ps1" -DllPath $dll -AcknowledgeVm -SnapshotAndPasswordFallbackVerified
        $state.Stage = 'Installed'
        $state | ConvertTo-Json | Set-Content -LiteralPath $checkpointPath -Encoding UTF8
    }
    if (-not (Test-Path -LiteralPath $installedDll) -or
        (Get-FileHash -LiteralPath $installedDll -Algorithm SHA256).Hash -ne $state.DllHash) {
        throw 'Installed DLL does not match the tested build. Stop and check recovery instructions.'
    }
    Write-Host 'Enrollment opens next. Use a throwaway local VM account and its actual Windows password.'
    $enroll = Start-Process -FilePath $enrollPath -Wait -PassThru
    if ($enroll.ExitCode -ne 0) { throw 'Enrollment did not succeed. Read its error dialog; do not lock to test yet.' }
    if ((Read-Host 'Did the enrollment dialog report success? Type ENROLLED (Cancel is not success)') -cne 'ENROLLED') {
        throw 'Finish enrollment successfully before lock-screen testing.'
    }
    $demo = Start-Process -FilePath $demoPath -Wait -PassThru
    if ($demo.ExitCode -ne 0) { throw 'The desktop demo failed. Do not proceed to lock-screen testing.' }
    if ((Read-Host 'Did drawing and cancellation work in the desktop demo? Type DEMO-OK') -cne 'DEMO-OK') {
        throw 'Verify the desktop demo before lock-screen testing.'
    }
    Write-Host 'Now test lock/unlock, repeated success and Password fallback. These checks require you.' -ForegroundColor Cyan
    Write-Host 'Full checklist: docs/VM_ACCEPTANCE.md. Automated passes do not prove lock-screen or TPM behavior.'
    if ((Read-Host 'Ready to lock the VM? Type LOCK, or Enter to finish without locking') -ceq 'LOCK') {
        # Use the Windows menu shortcut instead of invoking undocumented rundll32 exports.
        Write-Host 'Press Windows+L inside the VM now (ensure VMware has keyboard focus).'
    }
    if ((Read-Host 'After testing, type COMPLETE to clear this workflow checkpoint; Enter keeps it for retry') -ceq 'COMPLETE') {
        Remove-Item -LiteralPath $checkpointPath -Force
    }
} catch {
    Write-Host ('STOPPED: ' + $_.Exception.Message) -ForegroundColor Red
    Write-Host 'Do not proceed past a failure. Use Password or revert the VM snapshot if needed.'
    exit 1
} finally {
    if ($Elevated) { Read-Host 'Press Enter to close this elevated test window' | Out-Null }
}
