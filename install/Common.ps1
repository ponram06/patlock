Set-StrictMode -Version Latest
$script:Clsid     = '{21d42e91-a8c0-4e63-b5d7-372d8585982c}'
$script:Marker    = 'Patlock Pattern Provider'
$script:ProvKey   = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$script:Clsid"
$script:ClassKey  = "HKLM:\SOFTWARE\Classes\CLSID\$script:Clsid"
$script:ArpKey    = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Patlock'
$script:DataKey   = 'HKLM:\SOFTWARE\Patlock'
$script:EventKey  = 'HKLM:\SYSTEM\CurrentControlSet\Services\EventLog\Application\Patlock'
$script:InstallDir = Join-Path $env:ProgramFiles 'Patlock'
$script:Crypto    = 'Microsoft Platform Crypto Provider'

function Assert-Elevated {
    param([string]$Script, [string[]]$Forward)
    $p = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if ($p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { return }
    $argList = @('-NoProfile','-ExecutionPolicy','Bypass','-File',"`"$Script`"") + $Forward
    Start-Process powershell.exe -Verb RunAs -ArgumentList $argList -Wait
    exit 0
}
function Remove-PatlockRegistration {
    # Unregister the tile first so Windows stops loading the provider.
    foreach ($k in @($script:ProvKey, $script:ClassKey)) {
        if (-not (Test-Path -LiteralPath $k)) { continue }
        if ((Get-Item -LiteralPath $k).GetValue('') -ne $script:Marker) { Write-Warning "Skipping $k (not owned by Patlock)."; continue }
        Remove-Item -LiteralPath $k -Recurse -Force; Write-Host "Removed $k"
    }
}
function Register-PatlockEventSource([string]$MessageFile) {
    New-Item -Path $script:EventKey -Force | Out-Null
    New-ItemProperty -LiteralPath $script:EventKey -Name EventMessageFile -Value $MessageFile -PropertyType ExpandString -Force | Out-Null
    New-ItemProperty -LiteralPath $script:EventKey -Name TypesSupported -Value 7 -PropertyType DWord -Force | Out-Null
}
function Remove-PatlockEventSource {
    if ((Test-Path -LiteralPath $script:EventKey) -and
        (Get-Item -LiteralPath $script:EventKey).GetValue('EventMessageFile') -like '*PatlockProvider.*.dll') {
        Remove-Item -LiteralPath $script:EventKey -Recurse -Force
    }
}
function Remove-FileOrSchedule([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    try { Remove-Item -LiteralPath $Path -Force -ErrorAction Stop; Write-Host "Deleted $Path" }
    catch {
        if (-not ('Patlock.Native' -as [type])) {
            Add-Type -Namespace Patlock -Name Native -MemberDefinition '[DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern bool MoveFileEx(string a, string b, int f);'
        }
        [void][Patlock.Native]::MoveFileEx($Path, $null, 4)  # delete at next reboot
        Write-Host "In use; will be deleted at reboot: $Path"
    }
}
