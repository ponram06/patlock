# Builds, tests, optionally signs, and packages a release into dist\Patlock-Release.zip.
[CmdletBinding()]
param([string]$CertThumbprint, [string]$TimestampUrl = 'http://timestamp.digicert.com')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/FindBuildTools.ps1"
$tools = Find-PatlockBuildTools
$repo = (Resolve-Path "$PSScriptRoot/..").Path
$build = Join-Path $repo 'build/release'
& $tools.CMake -S $repo -B $build -G $tools.Generator -A x64 "-DCMAKE_GENERATOR_INSTANCE=$($tools.Instance)"; if ($LASTEXITCODE) { throw 'Configure failed.' }
& $tools.CMake --build $build --config Release -- /m;                       if ($LASTEXITCODE) { throw 'Build failed.' }
& $tools.CTest --test-dir $build -C Release --output-on-failure;            if ($LASTEXITCODE) { throw 'Tests failed. Not packaging.' }

$out = Join-Path $repo 'dist/Patlock'
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Path $out -Force | Out-Null
$bin = Join-Path $build 'Release'
foreach ($f in 'PatlockProvider.dll','patlock_enroll.exe') { Copy-Item (Join-Path $bin $f) $out }
foreach ($f in 'Install-Patlock.ps1','Uninstall-Patlock.ps1','Common.ps1') { Copy-Item (Join-Path $PSScriptRoot $f) $out }
Copy-Item "$repo/README.md","$repo/docs/RECOVERY.md" $out
@'
@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Patlock.ps1" %*
'@ | Set-Content "$out/Install.cmd" -Encoding ascii
@'
@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Uninstall-Patlock.ps1" %*
'@ | Set-Content "$out/Uninstall.cmd" -Encoding ascii

if ($CertThumbprint) {   # sign the code that runs in LogonUI; unsigned builds are rejected by the installer
    $signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter signtool.exe | Where-Object FullName -like '*x64*' | Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $signtool) { throw 'signtool.exe not found (install the Windows SDK).' }
    foreach ($f in 'PatlockProvider.dll','patlock_enroll.exe') {
        & $signtool.FullName sign /sha1 $CertThumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 (Join-Path $out $f)
        if ($LASTEXITCODE) { throw "Signing $f failed." }
    }
} else { Write-Warning 'No -CertThumbprint: package is unsigned; Install needs -AllowUnsigned.' }

Get-ChildItem $out -File | Where-Object { $_.Extension -in '.dll','.exe' } |
    ForEach-Object { '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash, $_.Name } | Set-Content "$out/SHA256SUMS.txt" -Encoding ascii
$zip = Join-Path $repo 'dist/Patlock-Release.zip'
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path "$out/*" -DestinationPath $zip
Write-Host "Release ready: $zip"
