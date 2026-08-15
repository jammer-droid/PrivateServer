param(
    [string]$ExecutablePath = (Join-Path $PSScriptRoot "..\build\bin\NetworkRuntime\x64\Release\PrivateServer.NetworkRuntime.Benchmark.exe"),
    [string]$ConfigPath = (Join-Path $PSScriptRoot "..\config\world-host-benchmark.json"),
    [ValidateRange(1, 10)]
    [int]$RepeatCount = 1
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

[System.Management.Automation.PathInfo]$resolvedExecutable = Resolve-Path -LiteralPath $ExecutablePath
[System.Management.Automation.PathInfo]$resolvedConfig = Resolve-Path -LiteralPath $ConfigPath
[object]$controllerConfig = Get-Content -Raw -Encoding utf8 $resolvedConfig.Path | ConvertFrom-Json
[string]$configDirectory = Split-Path -Parent $resolvedConfig.Path
[string]$runsRoot = [System.IO.Path]::GetFullPath((Join-Path $configDirectory $controllerConfig.artifact.runsRoot))

if ($resolvedExecutable.Path.IndexOf("\x64\Release\", [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
    throw "Canonical baseline requires the x64 Release Benchmark executable: $($resolvedExecutable.Path)"
}

New-Item -ItemType Directory -Path $runsRoot -Force | Out-Null
[System.Collections.Generic.List[string]]$completedRunIds = @()

for ([int]$repeatIndex = 1; $repeatIndex -le $RepeatCount; ++$repeatIndex) {
    [System.Collections.Generic.HashSet[string]]$existingRunIds =
        [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    Get-ChildItem -LiteralPath $runsRoot -Directory | ForEach-Object {
        [void]$existingRunIds.Add($_.Name)
    }

    [string]$output = (& $resolvedExecutable.Path world-host-lifecycle --config $resolvedConfig.Path 2>&1 | Out-String)
    [int]$exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "World benchmark failed: repeat=$repeatIndex exitCode=$exitCode`n$output"
    }

    [System.IO.DirectoryInfo[]]$newRunDirectories = @(
        Get-ChildItem -LiteralPath $runsRoot -Directory | Where-Object { -not $existingRunIds.Contains($_.Name) }
    )
    if ($newRunDirectories.Count -ne 1) {
        throw "Expected exactly one new World benchmark run directory, found $($newRunDirectories.Count)"
    }

    [System.IO.DirectoryInfo]$runDirectory = $newRunDirectories[0]
    [string]$mergedPath = Join-Path $runDirectory.FullName "benchmark\merged.json"
    [object]$merged = Get-Content -Raw -Encoding utf8 $mergedPath | ConvertFrom-Json
    if ($merged.schema -ne "psnr.benchmark.world_host.merged" -or $merged.version -ne 1) {
        throw "Merged artifact schema mismatch: $mergedPath"
    }
    if ($merged.runId -ne $runDirectory.Name -or -not $merged.identity.consistent) {
        throw "Merged artifact identity mismatch: $mergedPath"
    }
    if (-not $merged.completeness.complete -or -not $merged.verdict.valid) {
        [string]$verdictErrors = $merged.verdict.errors -join "; "
        throw "Merged verdict is invalid: runId=$($runDirectory.Name) errors=$verdictErrors"
    }

    [object]$benchmarkExecutable = $merged.sources.benchmarkEffectiveConfig.executable
    [object]$worldExecutable = $merged.sources.runManifest.executable
    if ($benchmarkExecutable.buildConfiguration -ne "Release" -or $benchmarkExecutable.architecture -ne "x64" -or
        $worldExecutable.buildConfiguration -ne "Release") {
        throw "Merged build identity is not Release/x64: runId=$($runDirectory.Name)"
    }

    [object]$early = $merged.phases.early
    [object]$mid = $merged.phases.mid
    [object]$late = $merged.phases.late
    Write-Output (
        "world benchmark baseline: runId=$($runDirectory.Name) " +
        "earlyP99Ns=$($early.executionDurationNanoseconds.p99) earlyOverrunRatio=$($early.executionOverrunRatio) " +
        "midP99Ns=$($mid.executionDurationNanoseconds.p99) midOverrunRatio=$($mid.executionOverrunRatio) " +
        "lateP99Ns=$($late.executionDurationNanoseconds.p99) lateOverrunRatio=$($late.executionOverrunRatio)"
    )
    $completedRunIds.Add($runDirectory.Name)
}

Write-Output "canonical runsRoot: $runsRoot"
Write-Output "canonical runIds: $($completedRunIds -join ',')"
