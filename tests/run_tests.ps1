param([string]$ApplicationOutputRoot = '')
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

function AssertApplicationManifest([string]$application, [string]$name) {
    $manifestPath = Join-Path $testOutput "$name.manifest"
    $extract = '"' + $vcvars + '" >nul && mt /nologo -inputresource:"' + $application + '";#1 -out:"' + $manifestPath + '"'
    & $env:ComSpec /d /s /c $extract
    if ($LASTEXITCODE -ne 0) { throw "Extracting $name manifest failed." }
    [xml]$manifest = Get-Content -LiteralPath $manifestPath -Raw
    $namespaces = New-Object System.Xml.XmlNamespaceManager($manifest.NameTable)
    $namespaces.AddNamespace('asm', 'urn:schemas-microsoft-com:asm.v1')
    $namespaces.AddNamespace('asmv3', 'urn:schemas-microsoft-com:asm.v3')
    $namespaces.AddNamespace('dpi2005', 'http://schemas.microsoft.com/SMI/2005/WindowsSettings')
    $namespaces.AddNamespace('dpi2016', 'http://schemas.microsoft.com/SMI/2016/WindowsSettings')
    $dpiAware = $manifest.SelectSingleNode('//dpi2005:dpiAware', $namespaces)
    $dpiAwareness = $manifest.SelectSingleNode('//dpi2016:dpiAwareness', $namespaces)
    $privileges = $manifest.SelectSingleNode('//asmv3:requestedExecutionLevel', $namespaces)
    $controls = $manifest.SelectSingleNode('//asm:dependentAssembly/asm:assemblyIdentity[@name="Microsoft.Windows.Common-Controls" and @version="6.0.0.0"]', $namespaces)
    if (-not $dpiAware -or $dpiAware.InnerText.Trim() -ne 'true/pm' -or -not $dpiAwareness -or $dpiAwareness.InnerText.Trim() -ne 'PerMonitorV2,PerMonitor') {
        throw "$name does not contain the expected DPI declarations."
    }
    if (-not $privileges -or $privileges.level -ne 'asInvoker' -or $privileges.uiAccess -ne 'false' -or -not $controls) {
        throw "$name lost its privilege or Common Controls v6 declaration."
    }
    Write-Output "$name embedded DPI manifest verified."
}

Push-Location $projectRoot
try {
    $msbuild = Join-Path $visualStudio 'MSBuild\Current\Bin\MSBuild.exe'
    $releaseApplication = Join-Path $projectRoot 'Release\MinimizeWindows.exe'
    foreach ($configuration in @('Debug', 'Release')) {
        $outputDirectory = Join-Path $projectRoot $configuration
        $buildArguments = @((Join-Path $projectRoot 'MinimizeWindows.sln'), '-nologo', '-m', "-p:Configuration=$configuration", '-p:Platform=x86', '-verbosity:minimal')
        if ($ApplicationOutputRoot) {
            $outputDirectory = Join-Path ([System.IO.Path]::GetFullPath($ApplicationOutputRoot)) $configuration
            $buildArguments += "-p:OutDir=$outputDirectory\"
            if ($configuration -eq 'Release') { $releaseApplication = Join-Path $outputDirectory 'MinimizeWindows.exe' }
        }
        & $msbuild @buildArguments
        if ($LASTEXITCODE -ne 0) { throw "$configuration x86 build failed." }
        AssertApplicationManifest (Join-Path $outputDirectory 'MinimizeWindows.exe') $configuration
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
    $compile = '"' + $vcvars + '" >nul && rc /nologo /I src /fo build\tests\resources.res src\resources.rc'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Compiling license resources failed.' }
    $uiLibraries = ' build\tests\resources.res user32.lib gdi32.lib dwmapi.lib shell32.lib comctl32.lib ole32.lib oleaut32.lib uuid.lib imm32.lib'
    $displaySources = ' src\display_model.cpp src\display_page.cpp src\monitor_enumerator.cpp'
    $aboutSources = ' src\about_page.cpp src\licenses_dialog.cpp src\settings_page.cpp'
    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE' + $uiFlags + '/Fe:"' + $testOutput + '\tray_tests.exe" /Fo:build\tests\ tests\tray_tests.cpp' + $displaySources + $aboutSources + $uiLibraries + ' /link /subsystem:console /manifest:embed'
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

    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE' + $uiFlags + '/Fe:"' + $testOutput + '\tray_test_app.exe" /Fo:build\tests\ tests\tray_test_app.cpp src\main.cpp src\settings_window.cpp src\desktop_manager.cpp src\keyboard_hook.cpp' + $displaySources + $aboutSources + $uiLibraries + ' /link /subsystem:windows /manifest:embed /manifestinput:src\app.manifest'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Compiling isolated tray app failed.' }
    AssertApplicationManifest (Join-Path $testOutput 'tray_test_app.exe') 'tray-test-app'

    # Opt-in normal-desktop Shell check; --startup also opens Explorer, never injects global input.
    $compile = '"' + $vcvars + '" >nul && cl /nologo /EHsc /W4 /MT /DUNICODE /D_UNICODE /DWINVER=0x0601 /D_WIN32_WINNT=0x0601 /Fe:"' + $testOutput + '\tray_desktop_smoke.exe" /Fo:"' + $testOutput + '\tray_desktop_smoke.obj" tests\tray_desktop_smoke.cpp user32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib uuid.lib /link /subsystem:console'
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
    & (Join-Path $PSScriptRoot 'monitor_cli_tests.ps1') -ApplicationPath $releaseApplication
}
finally {
    Pop-Location
}
