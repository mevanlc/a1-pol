$ErrorActionPreference = "Stop"

$ProcessName = "a1-pol-mem"
$Percent = if ($env:A1_POL_MEM_PERCENT) { $env:A1_POL_MEM_PERCENT } else { "50" }
$ScriptDir = Split-Path -Parent $PSCommandPath
$RepoRoot = (Resolve-Path (Join-Path $ScriptDir "..")).Path

function Write-Stderr {
    param([string]$Message)
    [Console]::Error.WriteLine($Message)
}

function Test-BinFile {
    param([string]$Path)
    return $Path -and (Test-Path -LiteralPath $Path -PathType Leaf)
}

function Resolve-A1PolMem {
    if ($env:A1_POL_MEM_BIN) {
        if (Test-BinFile -Path $env:A1_POL_MEM_BIN) {
            return (Resolve-Path -LiteralPath $env:A1_POL_MEM_BIN).Path
        }
        return $null
    }

    if (Test-Path -LiteralPath (Join-Path $RepoRoot ".git") -PathType Container) {
        $Found = Get-ChildItem -LiteralPath $RepoRoot -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -eq $ProcessName -or $_.Name -eq "$ProcessName.exe" } |
            Sort-Object FullName |
            Select-Object -First 1

        if ($Found) {
            return $Found.FullName
        }
    }

    $Command = Get-Command $ProcessName -CommandType Application -ErrorAction SilentlyContinue
    if ($Command) {
        return $Command.Source
    }

    return $null
}

$Bin = Resolve-A1PolMem
if (-not $Bin) {
    Write-Stderr "error: could not locate $ProcessName; set A1_POL_MEM_BIN, build the repo, or add it to PATH"
    exit 1
}

& $Bin start $Percent
exit $LASTEXITCODE
