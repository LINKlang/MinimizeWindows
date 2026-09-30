$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) {
    throw 'Visual Studio C++ build tools were not found.'
}
$vcvars = Join-Path $visualStudio 'VC\Auxiliary\Build\vcvars32.bat'
$testOutput = Join-Path $projectRoot 'build\tests'
[System.IO.Directory]::CreateDirectory($testOutput) | Out-Null

Push-Location $projectRoot
try {
    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE /Fe:"' + $testOutput + '\behavior_tests.exe" /Fo:"' + $testOutput + '\behavior_tests.obj" tests\behavior_tests.cpp user32.lib dwmapi.lib'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) {
        throw 'Compiling behavior tests failed.'
    }
    & (Join-Path $testOutput 'behavior_tests.exe')
    if ($LASTEXITCODE -ne 0) {
        throw 'Behavior tests failed.'
    }

    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE /Fe:"' + $testOutput + '\window_integration_tests.exe" /Fo:build\tests\ tests\window_integration_tests.cpp src\desktop_manager.cpp src\keyboard_hook.cpp user32.lib dwmapi.lib'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) {
        throw 'Compiling native window tests failed.'
    }
    & (Join-Path $testOutput 'window_integration_tests.exe')
    if ($LASTEXITCODE -ne 0) {
        throw 'Native window tests failed.'
    }
}
finally {
    Pop-Location
}
