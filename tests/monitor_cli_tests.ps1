param([Parameter(Mandatory = $true)][string]$ApplicationPath)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$testOutput = Join-Path $projectRoot 'build\tests'
[System.IO.Directory]::CreateDirectory($testOutput) | Out-Null
$stdoutPath = Join-Path $testOutput 'monitor_cli.stdout.txt'
$stderrPath = Join-Path $testOutput 'monitor_cli.stderr.txt'
function Invoke-ExitMode([string]$Arguments, [int]$ExpectedExitCode) {
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $ApplicationPath
    $start.Arguments = $Arguments
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
            throw "Exit mode '$Arguments' started a message loop instead of exiting."
        }
        if ($monitorProcess.ExitCode -ne $ExpectedExitCode) {
            throw "Mode '$Arguments' returned $($monitorProcess.ExitCode), expected $ExpectedExitCode."
        }
        if (-not $stdoutCopy.Wait(5000) -or -not $stderrCopy.Wait(5000)) {
            throw 'The output streams did not close.'
        }
        $utf8 = New-Object System.Text.UTF8Encoding($false, $true)
        $bytes = $stdoutBytes.ToArray()
        $errorBytes = $stderrBytes.ToArray()
        foreach ($streamBytes in @($bytes, $errorBytes)) {
            if ($streamBytes.Length -ge 3 -and $streamBytes[0] -eq 0xEF -and $streamBytes[1] -eq 0xBB -and $streamBytes[2] -eq 0xBF) {
                throw 'Redirected output unexpectedly includes a BOM.'
            }
        }
        return [pscustomobject]@{
            OutputBytes = $bytes
            ErrorBytes = $errorBytes
            Text = $utf8.GetString($bytes)
            ErrorText = $utf8.GetString($errorBytes)
        }
    }
    finally {
        if (-not $monitorProcess.HasExited) { Stop-Process -Id $monitorProcess.Id -Force }
        $monitorProcess.Dispose()
        $stdoutBytes.Dispose()
        $stderrBytes.Dispose()
    }
}

$listing = Invoke-ExitMode '--list-monitors' 0
[System.IO.File]::WriteAllBytes($stdoutPath, $listing.OutputBytes)
[System.IO.File]::WriteAllBytes($stderrPath, $listing.ErrorBytes)
if ($listing.ErrorText.Length -ne 0) { throw "Unexpected listing error: $($listing.ErrorText)" }
if ($listing.Text -notmatch '(?m)^Active displays: \d+' -or $listing.Text -notmatch 'Coordinates: signed physical desktop pixels') {
    throw 'The redirected listing is incomplete.'
}
if ($listing.Text -notmatch 'Debug indices are not Windows Settings display numbers') {
    throw 'The diagnostic index contract was omitted.'
}

$help = Invoke-ExitMode '--help' 0
if ($help.ErrorText.Length -ne 0 -or $help.Text -notmatch 'Usage:' -or $help.Text -notmatch '--monitor') {
    throw 'Help output is incomplete or reported an error.'
}

$invalidArguments = @(
    '--monitor',
    '--monitor ""',
    '--monitor --help',
    '--monitor "\\.\DISPLAY1" --monitor "\\.\DISPLAY2"',
    '--monitor "\\.\DISPLAY1" --help',
    '--help --monitor "\\.\DISPLAY1"',
    '--list-monitors --monitor "\\.\DISPLAY1"',
    '--monitor "\\.\DISPLAY1" --list-monitors',
    '--list-monitors --list-monitors',
    '--help --list-monitors',
    '--unknown',
    '--monitor "\\.\DISPLAY_DOES_NOT_EXIST"',
    '--monitor 2'
)
foreach ($arguments in $invalidArguments) {
    $result = Invoke-ExitMode $arguments 1
    if ($result.Text.Length -ne 0 -or $result.ErrorText -notmatch 'MinimizeWindows:' -or $result.ErrorText -notmatch 'Usage:') {
        throw "Invalid arguments '$arguments' did not report an error and usage on stderr."
    }
}
Write-Output 'Monitor CLI listing/help/errors and UTF-8 redirect tests passed.'
