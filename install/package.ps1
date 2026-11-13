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

$distRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'dist'))
$destPath = [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDir))
if (-not $destPath.StartsWith($distRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDir must be a child folder under this repository''s dist directory.'
}
# Refuse links/junctions in every existing ancestor of the destination.
$ancestor = [IO.DirectoryInfo]::new($destPath)
while ($null -ne $ancestor) {
    if ($ancestor.Exists -and ($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Package output cannot traverse a junction or symbolic link.'
    }
    if ($ancestor.FullName -eq $repoRoot) { break }
    $ancestor = $ancestor.Parent
}
if (Test-Path -LiteralPath $destPath) {
    throw 'Package destination already exists. Choose a fresh child folder under dist; existing files are never recursively deleted.'
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
Copy-Item -LiteralPath "$repoRoot\docs\RELEASE_STATUS.md" -Destination $destPath -Force
Copy-Item -LiteralPath "$repoRoot\docs\TPM_VALIDATION.md" -Destination $destPath -Force
Copy-Item -LiteralPath "$repoRoot\docs\VM_ACCEPTANCE.md" -Destination $destPath -Force
# Public artifact hashes only; credential records and private keys are excluded.
$hashes = foreach ($item in $requiredFiles) {
    $hash = Get-FileHash -LiteralPath (Join-Path $destPath $item.Name) -Algorithm SHA256
    '{0}  {1}' -f $hash.Hash, $item.Name
}
$hashes | Set-Content -LiteralPath (Join-Path $destPath 'SHA256SUMS.txt') -Encoding ascii

# Create ZIP archive
$zipPath = Join-Path $repoRoot "dist\Patlock-$Configuration.zip"
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
Compress-Archive -Path "$destPath\*" -DestinationPath $zipPath -Force

Write-Host "Packaging complete:"
Write-Host "  Folder:  $destPath"
Write-Host "  Archive: $zipPath"
