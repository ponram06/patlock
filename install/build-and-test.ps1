# VM ONLY. Builds and runs tests; never registers or installs the provider.
[CmdletBinding()]
param([switch]$AcknowledgeVm)
. "$PSScriptRoot/VmGuard.ps1"
Assert-PatlockVm -AcknowledgeVm:$AcknowledgeVm -AllowNonAdmin
. "$PSScriptRoot/FindBuildTools.ps1"
$tools = Find-PatlockBuildTools
$repoRoot = (Resolve-Path -LiteralPath "$PSScriptRoot/..").Path
Push-Location -LiteralPath $repoRoot
try {
    Write-Host ('Using {0}: {1}' -f $tools.Generator, $tools.Instance)
    # Use a separate cache for each generator; never overwrite a cache created
    # for a different Visual Studio version or the existing installed build.
    $generatorId = ($tools.Generator + '-' + $tools.InstanceId) -replace '[^a-zA-Z0-9-]', ''
    $buildDir = Join-Path $repoRoot "build/test-$generatorId"
    & $tools.CMake -S . -B $buildDir -G $tools.Generator -A x64 "-DCMAKE_GENERATOR_INSTANCE=$($tools.Instance)"
    if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
    & $tools.CMake --build $buildDir --config Release -- /m
    if ($LASTEXITCODE) { throw 'Build failed. Do not install this output.' }
    & $tools.CTest --test-dir $buildDir -C Release --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed. Do not install this output.' }
    Write-Host 'Build and executable tests passed in this VM. Manual lock-screen acceptance is still required.'
    Write-Host ('Test build output: ' + $buildDir)
} finally { Pop-Location }
