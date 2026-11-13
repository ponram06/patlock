Set-StrictMode -Version Latest

function Find-PatlockBuildTools {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio Installer was not found. Install Visual Studio with Desktop development with C++ (including Windows SDK and CMake tools).'
    }
    $json = & $vswhere -all -products '*' -prerelease -format json
    if ($LASTEXITCODE -ne 0) { throw 'Visual Studio discovery failed.' }
    $instances = @(($json -join "`n") | ConvertFrom-Json)
    $candidates = @($instances | Where-Object {
        if (([version]$_.installationVersion).Major -lt 17) { return $false }
        $compilerRoot = Join-Path $_.installationPath 'VC/Tools/MSVC'
        @(Get-ChildItem -LiteralPath $compilerRoot -Directory -ErrorAction SilentlyContinue | Where-Object {
            Test-Path -LiteralPath (Join-Path $_.FullName 'bin/Hostx64/x64/cl.exe')
        }).Count -gt 0
    } | Sort-Object { [version]$_.installationVersion } -Descending)
    if (-not $candidates.Count) {
        $names = @($instances | ForEach-Object { $_.displayName }) -join ', '
        throw "No installed x64 MSVC compiler found. Detected installations: $names. Open Visual Studio Installer > Modify > Desktop development with C++; include MSVC x64/x86 tools, Windows SDK and CMake tools. Then rerun this launcher."
    }
    foreach ($instance in $candidates) {
        $major = ([version]$instance.installationVersion).Major
        $cmakePaths = @()
        $onPath = Get-Command cmake.exe -ErrorAction SilentlyContinue
        if ($onPath) { $cmakePaths += $onPath.Source }
        $cmakePaths += Join-Path $instance.installationPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
        foreach ($cmake in @($cmakePaths | Select-Object -Unique)) {
            if (-not (Test-Path -LiteralPath $cmake)) { continue }
            $ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
            if (-not (Test-Path -LiteralPath $ctest)) { continue }
            $helpText = (& $cmake --help) -join "`n"
            if ($LASTEXITCODE -ne 0) { continue }
            $pattern = '(?m)^\s*\*?\s*(Visual Studio ' + $major + ' \d+)\s*='
            $match = [regex]::Match($helpText, $pattern)
            if ($match.Success) {
                return [pscustomobject]@{ CMake = $cmake; CTest = $ctest
                    Generator = $match.Groups[1].Value; Instance = $instance.installationPath
                    InstanceId = $instance.instanceId }
            }
        }
    }
    throw 'CMake/CTest supporting your installed Visual Studio version were not found. Update CMake or install the C++ CMake tools component in Visual Studio Installer.'
}
