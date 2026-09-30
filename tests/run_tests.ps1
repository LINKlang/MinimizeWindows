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
    $msbuild = Join-Path $visualStudio 'MSBuild\Current\Bin\MSBuild.exe'
    foreach ($configuration in @('Debug', 'Release')) {
        & $msbuild (Join-Path $projectRoot 'MinimizeWindows.sln') -nologo -m "-p:Configuration=$configuration" -p:Platform=x86 -verbosity:minimal
        if ($LASTEXITCODE -ne 0) { throw "$configuration x86 build failed." }
    }
    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE /Fe:"' + $testOutput + '\behavior_tests.exe" /Fo:"' + $testOutput + '\behavior_tests.obj" tests\behavior_tests.cpp user32.lib dwmapi.lib'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) {
        throw 'Compiling behavior tests failed.'
    }
    & (Join-Path $testOutput 'behavior_tests.exe')
    if ($LASTEXITCODE -ne 0) {
        throw 'Behavior tests failed.'
    }

    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE /Fe:"' + $testOutput + '\window_integration_tests.exe" /Fo:build\tests\ tests\window_integration_tests.cpp src\desktop_manager.cpp src\keyboard_hook.cpp user32.lib dwmapi.lib imm32.lib'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) {
        throw 'Compiling native window tests failed.'
    }
    & (Join-Path $testOutput 'window_integration_tests.exe') (Join-Path $projectRoot 'Release\MinimizeWindows.exe')
    if ($LASTEXITCODE -ne 0) {
        throw 'Native window tests failed.'
    }

    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE /DWINVER=0x0601 /D_WIN32_WINNT=0x0601 /Fe:"' + $testOutput + '\monitor_tests.exe" /Fo:"' + $testOutput + '\monitor_tests.obj" tests\monitor_tests.cpp user32.lib'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Compiling monitor boundary tests failed.' }
    & (Join-Path $testOutput 'monitor_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Monitor boundary tests failed.' }

    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE /DWINVER=0x0601 /D_WIN32_WINNT=0x0601 /Fe:"' + $testOutput + '\monitor_integration_tests.exe" /Fo:build\tests\ tests\monitor_integration_tests.cpp src\monitor_enumerator.cpp src\desktop_manager.cpp src\keyboard_hook.cpp user32.lib dwmapi.lib shell32.lib /link /subsystem:console'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Compiling native monitor tests failed.' }
    & (Join-Path $testOutput 'monitor_integration_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Native monitor tests failed.' }
    & (Join-Path $PSScriptRoot 'monitor_cli_tests.ps1') -ApplicationPath (Join-Path $projectRoot 'Release\MinimizeWindows.exe')
}
finally {
    Pop-Location
}
