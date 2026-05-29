$ErrorActionPreference = "Continue"

$ProcessName = "a1-pol-mem"

function Write-Stderr {
    param([string]$Message)
    [Console]::Error.WriteLine($Message)
}

$Processes = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
if ($Processes.Count -eq 0) {
    exit 0
}

foreach ($Process in $Processes) {
    try {
        Stop-Process -Id $Process.Id -Force -ErrorAction Stop
    } catch {
        Write-Stderr "warning: could not stop $ProcessName process $($Process.Id): $($_.Exception.Message)"
    }
}

Start-Sleep -Seconds 2

foreach ($Process in $Processes) {
    try {
        $StillRunning = Get-Process -Id $Process.Id -ErrorAction Stop
        if ($StillRunning.ProcessName -eq $ProcessName) {
            Write-Stderr "warning: $ProcessName process $($Process.Id) is still running after stop request"
        }
    } catch {
    }
}

exit 0
