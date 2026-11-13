# VM ONLY. Builds and runs tests; never registers or installs the provider.
[CmdletBinding()]
param([switch]$AcknowledgeVm)
. "$PSScriptRoot/VmGuard.ps1"
Assert-PatlockVm -AcknowledgeVm:$AcknowledgeVm
$repoRoot = (Resolve-Path -LiteralPath "$PSScriptRoot/..").Path
Push-Location -LiteralPath $repoRoot
try {
    & cmake -S . -B build -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
    & cmake --build build --config Release -- /m
    if ($LASTEXITCODE) { throw 'Build failed. Do not install this output.' }
    & ctest --test-dir build -C Release --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed. Do not install this output.' }
    Write-Host 'Build and executable tests passed in this VM. Manual lock-screen acceptance is still required.'
} finally { Pop-Location }
