$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) {
    throw 'Visual Studio C++ build tools were not found.'
}
$vcvars = Join-Path $visualStudio 'VC\Auxiliary\Build\vcvars32.bat'
$testOutput = Join-Path $projectRoot 'build\tests'
$wtlInclude = Join-Path $projectRoot 'third_party\wtl\Include'
$jsonInclude = Join-Path $projectRoot 'third_party'
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

    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE /DWINVER=0x0601 /D_WIN32_WINNT=0x0601 /Fe:"' + $testOutput + '\startup_tests.exe" /Fo:"' + $testOutput + '\startup_tests.obj" tests\startup_tests.cpp user32.lib dwmapi.lib shell32.lib /link /subsystem:console'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Compiling startup tests failed.' }
    & (Join-Path $testOutput 'startup_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Startup tests failed.' }

    $jsonFlags = ' /utf-8 /I"' + $jsonInclude + '" '
    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE /DWINVER=0x0601 /D_WIN32_WINNT=0x0601' + $jsonFlags + '/Fe:"' + $testOutput + '\config_tests.exe" /Fo:build\tests\ tests\config_tests.cpp src\config_store.cpp shell32.lib ole32.lib uuid.lib /link /subsystem:console'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Compiling configuration tests failed.' }
    & (Join-Path $testOutput 'config_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Configuration tests failed.' }

    $uiFlags = ' /utf-8 /DWINVER=0x0601 /D_WIN32_WINNT=0x0601 /I"' + $wtlInclude + '" '
    $uiLibraries = ' user32.lib gdi32.lib dwmapi.lib shell32.lib comctl32.lib ole32.lib oleaut32.lib imm32.lib'
    $displaySources = ' src\display_model.cpp src\display_page.cpp src\monitor_enumerator.cpp'
    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE' + $uiFlags + '/Fe:"' + $testOutput + '\tray_tests.exe" /Fo:build\tests\ tests\tray_tests.cpp' + $displaySources + $uiLibraries + ' /link /subsystem:console /manifest:embed'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Compiling WTL tray tests failed.' }
    $trayStart = New-Object System.Diagnostics.ProcessStartInfo
    $trayStart.FileName = Join-Path $testOutput 'tray_tests.exe'
    $trayStart.UseShellExecute = $false
    $trayStart.CreateNoWindow = $true
    $trayStart.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
    $trayStart.RedirectStandardOutput = $true
    $trayStart.RedirectStandardError = $true
    $trayTestProcess = [System.Diagnostics.Process]::Start($trayStart)
    try {
        $trayFinished = $trayTestProcess.WaitForExit(15000)
        if (-not $trayFinished) { $trayTestProcess.Kill(); $trayTestProcess.WaitForExit() }
        $trayOutput = $trayTestProcess.StandardOutput.ReadToEnd()
        $trayError = $trayTestProcess.StandardError.ReadToEnd()
        Write-Output $trayOutput
        if ($trayError.Length -ne 0) { Write-Output $trayError }
        if (-not $trayFinished) { throw 'WTL tray tests timed out.' }
        if ($trayTestProcess.ExitCode -ne 0) { throw 'WTL tray tests failed.' }
    }
    finally { $trayTestProcess.Dispose() }

    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE' + $uiFlags + '/Fe:"' + $testOutput + '\display_tests.exe" /Fo:build\tests\ tests\display_tests.cpp src\display_model.cpp src\settings_window.cpp src\monitor_enumerator.cpp' + $uiLibraries + ' /link /subsystem:console /manifest:embed'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Compiling Display page tests failed.' }
    & (Join-Path $testOutput 'display_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Display page tests failed.' }

    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE' + $uiFlags + '/Fe:"' + $testOutput + '\tray_test_app.exe" /Fo:build\tests\ tests\tray_test_app.cpp src\main.cpp src\settings_window.cpp src\desktop_manager.cpp src\keyboard_hook.cpp' + $displaySources + $uiLibraries + ' /link /subsystem:windows /manifest:embed'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Compiling isolated tray app failed.' }

    # Opt-in normal-desktop Shell check; opens only its own frame, never injects global input.
    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE /DWINVER=0x0601 /D_WIN32_WINNT=0x0601 /Fe:"' + $testOutput + '\tray_desktop_smoke.exe" /Fo:"' + $testOutput + '\tray_desktop_smoke.obj" tests\tray_desktop_smoke.cpp user32.lib shell32.lib /link /subsystem:console'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Compiling desktop tray smoke test failed.' }

    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE /Fe:"' + $testOutput + '\window_integration_tests.exe" /Fo:build\tests\ tests\window_integration_tests.cpp src\desktop_manager.cpp src\keyboard_hook.cpp user32.lib dwmapi.lib imm32.lib'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) {
        throw 'Compiling native window tests failed.'
    }
    & (Join-Path $testOutput 'window_integration_tests.exe') (Join-Path $testOutput 'tray_test_app.exe')
    if ($LASTEXITCODE -ne 0) {
        throw 'Native window tests failed.'
    }

    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE /DWINVER=0x0601 /D_WIN32_WINNT=0x0601 /Fe:"' + $testOutput + '\monitor_tests.exe" /Fo:"' + $testOutput + '\monitor_tests.obj" tests\monitor_tests.cpp user32.lib'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Compiling monitor boundary tests failed.' }
    & (Join-Path $testOutput 'monitor_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Monitor boundary tests failed.' }

    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE /DWINVER=0x0601 /D_WIN32_WINNT=0x0601' + $jsonFlags + '/Fe:"' + $testOutput + '\monitor_integration_tests.exe" /Fo:build\tests\ tests\monitor_integration_tests.cpp src\monitor_enumerator.cpp src\desktop_manager.cpp src\keyboard_hook.cpp src\config_store.cpp user32.lib dwmapi.lib shell32.lib ole32.lib uuid.lib /link /subsystem:console'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Compiling native monitor tests failed.' }
    & (Join-Path $testOutput 'monitor_integration_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Native monitor tests failed.' }
    & (Join-Path $PSScriptRoot 'monitor_cli_tests.ps1') -ApplicationPath (Join-Path $projectRoot 'Release\MinimizeWindows.exe')
}
finally {
    Pop-Location
}
