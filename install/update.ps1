# Run ONLY inside the Windows 11 VMware VM as Administrator.
# Builds the latest code and safely deploys PatlockProvider.dll even if locked by LogonUI.
[CmdletBinding()]
param(
    [switch]$AcknowledgeVm
)
. "$PSScriptRoot/VmGuard.ps1"
Assert-PatlockVm -AcknowledgeVm:$AcknowledgeVm

Write-Host "Stopping any running test tools..." -ForegroundColor Cyan
Stop-Process -Name patlock_enroll, pattern_window_demo -Force -ErrorAction SilentlyContinue

Write-Host "Building Release configuration..." -ForegroundColor Cyan
$buildResult = Start-Process -FilePath "cmake" -ArgumentList "--build", "build", "--config", "Release" -Wait -PassThru -NoNewWindow
if ($buildResult.ExitCode -ne 0) {
    throw "Build failed with exit code $($buildResult.ExitCode)."
}

$source = Join-Path $PSScriptRoot "..\build\Release\PatlockProvider.dll"
$destination = Join-Path $env:SystemRoot "System32\PatlockProvider.dll"
$backup = Join-Path $env:SystemRoot "System32\PatlockProvider.dll.old"

if (-not (Test-Path -LiteralPath $source)) {
    throw "Build artifact not found: $source"
}

Write-Host "Deploying PatlockProvider.dll to System32..." -ForegroundColor Cyan

# Terminate logonui if it is holding the DLL open
Stop-Process -Name logonui -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

$copied = $false
try {
    Copy-Item -LiteralPath $source -Destination $destination -Force -ErrorAction Stop
    $copied = $true
} catch {
    Write-Warning "Direct copy failed (file locked). Renaming in-use DLL..."
    Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue
    Move-Item -LiteralPath $destination -Destination $backup -Force -ErrorAction Stop
    Copy-Item -LiteralPath $source -Destination $destination -Force -ErrorAction Stop
    $copied = $true
}

# Verify deployment
$sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$destHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash

if ($sourceHash -ne $destHash) {
    throw "Verification failed: System32 DLL hash does not match build artifact!"
}

$fileInfo = Get-Item -LiteralPath $destination
Write-Host ""
Write-Host "============================================================" -ForegroundColor Green
Write-Host " SUCCESS! PatlockProvider.dll successfully updated in System32" -ForegroundColor Green
Write-Host " Installed: $($fileInfo.FullName)" -ForegroundColor Green
Write-Host " Timestamp: $($fileInfo.LastWriteTime)" -ForegroundColor Green
Write-Host " SHA-256:   $destHash" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Ready to test:" -ForegroundColor Yellow
Write-Host "1. Press Win + L to lock screen." -ForegroundColor Yellow
Write-Host "2. Select your account -> click 'Draw pattern'." -ForegroundColor Yellow
Write-Host "3. Note the title: 'Patlock - Pattern Unlock (Glide Mode)'." -ForegroundColor Yellow
Write-Host "4. Glide across 5+ dots: rubber-band line follows cursor." -ForegroundColor Yellow
Write-Host "5. Pause on final dot: automatically unlocks and logs in!" -ForegroundColor Yellow
Write-Host "   (If wrong pattern: instant error dialog is shown)." -ForegroundColor Yellow
