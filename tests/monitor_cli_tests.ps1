param([Parameter(Mandatory = $true)][string]$ApplicationPath)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$testOutput = Join-Path $projectRoot 'build\tests'
[System.IO.Directory]::CreateDirectory($testOutput) | Out-Null
$stdoutPath = Join-Path $testOutput 'monitor_cli.stdout.txt'
$stderrPath = Join-Path $testOutput 'monitor_cli.stderr.txt'
$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = $ApplicationPath
$start.Arguments = '--list-monitors'
$start.WorkingDirectory = $projectRoot
$start.UseShellExecute = $false
$start.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$monitorProcess = [System.Diagnostics.Process]::Start($start)
$stdoutBytes = New-Object System.IO.MemoryStream
$stderrBytes = New-Object System.IO.MemoryStream
$stdoutCopy = $monitorProcess.StandardOutput.BaseStream.CopyToAsync($stdoutBytes)
$stderrCopy = $monitorProcess.StandardError.BaseStream.CopyToAsync($stderrBytes)
try {
    if (-not $monitorProcess.WaitForExit(5000)) {
        throw 'The listing mode did not exit; it must not start the hook message loop.'
    }
    if ($monitorProcess.ExitCode -ne 0) {
        throw "The listing mode returned $($monitorProcess.ExitCode)."
    }
    if (-not $stdoutCopy.Wait(5000) -or -not $stderrCopy.Wait(5000)) {
        throw 'The listing output streams did not close.'
    }
    $utf8 = New-Object System.Text.UTF8Encoding($false, $true)
    $bytes = $stdoutBytes.ToArray()
    $errorBytes = $stderrBytes.ToArray()
    [System.IO.File]::WriteAllBytes($stdoutPath, $bytes)
    [System.IO.File]::WriteAllBytes($stderrPath, $errorBytes)
    $text = $utf8.GetString($bytes)
    $errorText = $utf8.GetString($errorBytes)
    if ($errorText.Length -ne 0) { throw "Unexpected listing error: $errorText" }
    if ($text -notmatch '(?m)^Active displays: \d+' -or $text -notmatch 'Coordinates: signed physical desktop pixels') {
        throw 'The redirected listing is incomplete.'
    }
    if ($text -notmatch 'Debug indices are not Windows Settings display numbers') {
        throw 'The diagnostic index contract was omitted.'
    }
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        throw 'The redirected listing unexpectedly includes a BOM.'
    }
    Write-Output 'Monitor CLI redirect/exit test passed.'
}
finally {
    if (-not $monitorProcess.HasExited) { Stop-Process -Id $monitorProcess.Id -Force }
    $monitorProcess.Dispose()
    $stdoutBytes.Dispose()
    $stderrBytes.Dispose()
}
