# Run ONLY inside the Windows 11 VMware VM as Administrator.
# Builds the latest code, runs the tests, and deploys PatlockProvider.dll only if
# every test passed. Finds Visual Studio and CMake itself (no developer prompt needed).
[CmdletBinding()]
param(
    [switch]$AcknowledgeVm
)
. "$PSScriptRoot/VmGuard.ps1"
Assert-PatlockVm -AcknowledgeVm:$AcknowledgeVm

Write-Host "Stopping any running test tools..." -ForegroundColor Cyan
Stop-Process -Name patlock_enroll, pattern_window_demo -Force -ErrorAction SilentlyContinue

. "$PSScriptRoot/FindBuildTools.ps1"
$tools = Find-PatlockBuildTools
$repoRoot = (Resolve-Path -LiteralPath "$PSScriptRoot/..").Path
$buildDir = Join-Path $repoRoot 'build'
Write-Host ('Using {0}: {1}' -f $tools.Generator, $tools.Instance) -ForegroundColor Cyan

# Configure once. An existing build folder keeps its generator; cmake --build
# re-runs configuration by itself when CMakeLists.txt changes.
if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'CMakeCache.txt'))) {
    Write-Host "Configuring a new build folder..." -ForegroundColor Cyan
    & $tools.CMake -S $repoRoot -B $buildDir -G $tools.Generator -A x64 "-DCMAKE_GENERATOR_INSTANCE=$($tools.Instance)"
    if ($LASTEXITCODE) { throw 'CMake configuration failed. Nothing was installed.' }
}

Write-Host "Building Release configuration..." -ForegroundColor Cyan
& $tools.CMake --build $buildDir --config Release -- /m
if ($LASTEXITCODE) { throw 'Build failed. Nothing was installed.' }

Write-Host "Running tests..." -ForegroundColor Cyan
& $tools.CTest --test-dir $buildDir -C Release --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed. Nothing was installed; the previous provider is still in place.' }

$source = Join-Path $buildDir 'Release\PatlockProvider.dll'
$destination = Join-Path $env:SystemRoot "System32\PatlockProvider.dll"
$backup = Join-Path $env:SystemRoot "System32\PatlockProvider.dll.old"

if (-not (Test-Path -LiteralPath $source)) {
    throw "Build artifact not found: $source"
}

Write-Host "Deploying PatlockProvider.dll to System32..." -ForegroundColor Cyan

# LogonUI is never terminated here: killing it can leave the session without a
# working lock screen. A loaded DLL is renamed aside below and replaced on next load.
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

# Ensure provider registration in Windows Credential Providers
$clsid = '{21d42e91-a8c0-4e63-b5d7-372d8585982c}'
$providerKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$clsid"
$classKey = "HKLM:\SOFTWARE\Classes\CLSID\$clsid"
if (-not (Test-Path -LiteralPath $providerKey) -or -not (Test-Path -LiteralPath $classKey)) {
    # Register exactly as install.ps1 does (full path plus ownership markers), so
    # uninstall.ps1 recognises the registration and can remove it.
    Write-Host "Registering Patlock Credential Provider in Windows..." -ForegroundColor Cyan
    $marker = 'Patlock Pattern Provider'
    New-Item -Path "$classKey\InprocServer32" -Force | Out-Null
    Set-Item -LiteralPath $classKey -Value $marker
    Set-Item -LiteralPath "$classKey\InprocServer32" -Value $destination
    New-ItemProperty -LiteralPath "$classKey\InprocServer32" -Name ThreadingModel -Value Apartment -PropertyType String -Force | Out-Null
    # Register the tile last. No provider filter or built-in provider is touched.
    New-Item -Path $providerKey -Force | Out-Null
    Set-Item -LiteralPath $providerKey -Value $marker
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
Write-Host " Policy:    Option A (survives reboots, no 24h expiration)" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Ready to test:" -ForegroundColor Yellow
Write-Host "1. Press Win + L to lock screen (or restart the VM - Pattern stays active!)." -ForegroundColor Yellow
Write-Host "2. Select your account -> click Pattern sign-in option." -ForegroundColor Yellow
Write-Host "3. Glide across 5+ dots: rubber-band follows cursor cleanly." -ForegroundColor Yellow
Write-Host "4. Complete pattern: instantly unlocks and logs in!" -ForegroundColor Yellow
Write-Host "   (If you ever need to change pattern, run: $(Join-Path $buildDir 'Release\patlock_enroll.exe'))" -ForegroundColor Yellow
