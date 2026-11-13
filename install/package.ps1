# Patlock Distribution Packaging Script
# Creates a standalone release package from build output.
[CmdletBinding()]
param(
    [string]$Configuration = 'Release',
    [string]$OutputDir = 'dist\Patlock'
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$buildDir = Join-Path $repoRoot "build\$Configuration"

$requiredFiles = @(
    @{ Source = "$buildDir\PatlockProvider.dll"; Name = "PatlockProvider.dll" },
    @{ Source = "$buildDir\patlock_enroll.exe"; Name = "patlock_enroll.exe" },
    @{ Source = "$buildDir\pattern_window_demo.exe"; Name = "pattern_window_demo.exe" }
)

Write-Host "Verifying build outputs in: $buildDir"
foreach ($item in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $item.Source)) {
        throw "Missing required build artifact: $($item.Source). Build the project first with: cmake --build build --config $Configuration"
    }
}

$destPath = Join-Path $repoRoot $OutputDir
if (Test-Path -LiteralPath $destPath) {
    Remove-Item -LiteralPath $destPath -Recurse -Force
}
New-Item -ItemType Directory -Path $destPath -Force | Out-Null

# Copy binaries
foreach ($item in $requiredFiles) {
    Copy-Item -LiteralPath $item.Source -Destination (Join-Path $destPath $item.Name) -Force
    Write-Host "  Copied: $($item.Name)"
}

# Copy install scripts and registry files
$installDir = Join-Path $destPath 'install'
New-Item -ItemType Directory -Path $installDir -Force | Out-Null
Copy-Item -LiteralPath "$PSScriptRoot\install.ps1" -Destination $installDir -Force
Copy-Item -LiteralPath "$PSScriptRoot\uninstall.ps1" -Destination $installDir -Force
Copy-Item -LiteralPath "$PSScriptRoot\VmGuard.ps1" -Destination $installDir -Force
Copy-Item -LiteralPath "$PSScriptRoot\register.reg" -Destination $installDir -Force
Copy-Item -LiteralPath "$PSScriptRoot\unregister.reg" -Destination $installDir -Force

# Copy recovery documentation
Copy-Item -LiteralPath "$repoRoot\docs\RECOVERY.md" -Destination $destPath -Force
Copy-Item -LiteralPath "$repoRoot\README.md" -Destination $destPath -Force

# Create ZIP archive
$zipPath = Join-Path $repoRoot "dist\Patlock-$Configuration.zip"
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
Compress-Archive -Path "$destPath\*" -DestinationPath $zipPath -Force

Write-Host "Packaging complete:"
Write-Host "  Folder:  $destPath"
Write-Host "  Archive: $zipPath"
