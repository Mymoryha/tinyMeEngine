[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('doctor', 'configure', 'build', 'test', 'verify', 'gpu-test')]
    [string]$Action = 'verify',
    [ValidateSet('Debug', 'RelWithDebInfo')]
    [string]$Configuration = 'Debug'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Invoke-Native {
    param([string]$Executable, [string[]]$Arguments)
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable failed with exit code $LASTEXITCODE."
    }
}

Push-Location $root
try {
    if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitOperatingSystem) {
        throw 'TinyMeEngine requires Windows x64.'
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'Install Visual Studio 2022 with Desktop development with C++.'
    }
    $vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $vs) {
        throw 'Visual Studio 2022 C++ tools were not found.'
    }
    $vs = ($vs | Select-Object -First 1).Trim()

    $requirements = Get-Content (Join-Path $PSScriptRoot 'requirements-build.txt')
    $pin = @($requirements | Where-Object { $_ -match '^cmake==([0-9.]+)$' })
    if ($pin.Count -ne 1) {
        throw 'Expected one exact CMake pin in requirements-build.txt.'
    }
    $requiredVersion = $pin[0].Substring('cmake=='.Length)
    $candidates = @((Join-Path $root '.tools\python\cmake\data\bin\cmake.exe'))
    $pathCMake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($pathCMake) {
        $candidates += $pathCMake.Source
    }
    $candidates += Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    $cmake = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $cmake) {
        throw 'CMake is missing. See README.md for the project-local installation command.'
    }
    $versionOutput = & $cmake --version
    if ($LASTEXITCODE -ne 0 -or $versionOutput[0] -ne "cmake version $requiredVersion") {
        throw "Expected CMake $requiredVersion. Use the pinned project-local tool; see README.md."
    }
    $ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
    if (-not (Test-Path $ctest)) {
        throw 'CTest is missing from the selected CMake installation.'
    }

    $presets = Get-Content (Join-Path $root 'CMakePresets.json') -Raw | ConvertFrom-Json
    $platform = $presets.configurePresets[0].architecture
    if ($platform -notmatch '^x64,version=([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)$') {
        throw 'The Windows preset must explicitly pin an x64 SDK version.'
    }
    $sdkVersion = $Matches[1]
    $kits = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots'
    $sdkHeader = Join-Path $kits.KitsRoot10 "Include\$sdkVersion\um\d3d12.h"
    $sdkLibrary = Join-Path $kits.KitsRoot10 "Lib\$sdkVersion\um\x64\kernel32.lib"
    if (-not (Test-Path $sdkHeader) -or -not (Test-Path $sdkLibrary)) {
        throw "Windows SDK $sdkVersion x64 is missing. See Docs/ENVIRONMENT.md."
    }

    Write-Host "VS2022: $vs"
    Write-Host "CMake: $cmake ($requiredVersion)"
    Write-Host "Windows SDK: $sdkVersion"
    if ($Action -ne 'doctor') {
        Invoke-Native $cmake @('--preset', 'windows-vs2022', "-DCMAKE_GENERATOR_INSTANCE=$vs")
        if ($Action -ne 'configure') {
            $preset = if ($Configuration -eq 'Debug') { 'windows-debug' } else { 'windows-profile' }
            Invoke-Native $cmake @('--build', '--preset', $preset, '--parallel')
            if ($Action -in @('test', 'verify', 'gpu-test')) {
                Invoke-Native $ctest @('--preset', $preset, '-LE', 'GPU')
            }
            if ($Action -eq 'gpu-test') {
                Write-Host 'GPU validation opens desktop windows; RTX 5080 and Graphics Tools are required.'
                $gpuTests = Join-Path $root "out\build\windows-vs2022\bin\$Configuration\tinyme_gpu_tests.exe"
                Invoke-Native -Executable $gpuTests -Arguments @()
                Invoke-Native -Executable $gpuTests -Arguments @('--gpu-validation')
                $postTests = Join-Path $root "out\build\windows-vs2022\bin\$Configuration\tinyme_post_gpu_tests.exe"
                Invoke-Native -Executable $postTests -Arguments @()
                Invoke-Native -Executable $postTests -Arguments @('--gpu-validation')
                foreach ($app in @('tinyme_runtime', 'tinyme_editor')) {
                    $program = Join-Path $root "out\build\windows-vs2022\bin\$Configuration\$app.exe"
                    if (Test-Path $program) {
                        Invoke-Native $cmake @("-DPROGRAM=$program", '-P', (Join-Path $root 'tests\CheckWindowCli.cmake'))
                        Invoke-Native $cmake @("-DPROGRAM=$program", '-P', (Join-Path $root 'tests\CheckProfileCli.cmake'))
                        Invoke-Native $cmake @("-DPROGRAM=$program", '-P', (Join-Path $root 'tests\CheckM3Cli.cmake'))
                    }
                }
            }
        }
    }
}
catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
finally {
    Pop-Location
}
exit 0
