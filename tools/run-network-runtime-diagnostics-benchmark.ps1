param(
    [string]$ExecutablePath = (Join-Path $PSScriptRoot "..\build\bin\NetworkRuntime\x64\Release\PrivateServer.NetworkRuntime.Smoke.exe"),
    [ValidateRange(1, 100)]
    [int]$RepeatCount = 5,
    [string]$ArtifactDirectory = (Join-Path $PSScriptRoot "..\build\artifacts\diagnostics-benchmark")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-Median {
    param([double[]]$Values)

    [double[]]$sortedValues = @($Values | Sort-Object)
    [int]$middleIndex = [int][Math]::Floor($sortedValues.Count / 2)
    if (($sortedValues.Count % 2) -eq 1) {
        return $sortedValues[$middleIndex]
    }

    return ($sortedValues[$middleIndex - 1] + $sortedValues[$middleIndex]) / 2.0
}

function Get-NearestRankPercentile {
    param(
        [double[]]$Values,
        [ValidateRange(0.0, 1.0)]
        [double]$Percentile
    )

    [double[]]$sortedValues = @($Values | Sort-Object)
    [int]$rank = [Math]::Max(1, [int][Math]::Ceiling($Percentile * $sortedValues.Count))
    return $sortedValues[$rank - 1]
}

[System.Management.Automation.PathInfo]$resolvedExecutable = Resolve-Path -LiteralPath $ExecutablePath
[string]$runDirectoryName = [DateTime]::UtcNow.ToString("yyyyMMdd-HHmmss-fff")
[string]$runArtifactDirectory = Join-Path $ArtifactDirectory $runDirectoryName
New-Item -ItemType Directory -Path $runArtifactDirectory -Force | Out-Null

[string[]]$modes = @("disabled", "debug", "benchmark")
foreach ($mode in $modes) {
    [System.Collections.Generic.List[double]]$elapsedMilliseconds = @()
    [System.Collections.Generic.List[double]]$artifactBytes = @()
    [string]$counterSignature = ""
    [string]$attempted = ""
    [string]$enqueued = ""
    [string]$consumed = ""
    [string]$droppedQueueFull = ""
    [string]$droppedSinkUnavailable = ""
    [string]$discardedAfterSinkFailure = ""
    [string]$sinkFailed = ""

    for ([int]$runIndex = 1; $runIndex -le $RepeatCount; ++$runIndex) {
        [string[]]$smokeArguments = @($mode)
        if ($mode -eq "benchmark") {
            [string]$artifactPath = Join-Path $runArtifactDirectory ("benchmark-{0:D2}.jsonl" -f $runIndex)
            $smokeArguments += $artifactPath
        }

        [System.Diagnostics.Stopwatch]$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        [string]$output = (& $resolvedExecutable.Path @smokeArguments 2>&1 | Out-String)
        [int]$exitCode = $LASTEXITCODE
        $stopwatch.Stop()

        if ($exitCode -ne 0) {
            throw "Smoke failed: mode=$mode run=$runIndex exitCode=$exitCode`n$output"
        }

        [System.Text.RegularExpressions.Match]$diagnosticsMatch = [regex]::Match(
            $output,
            "diagnostics evidence: mode=(disabled|debug|benchmark) attempted=(\d+) enqueued=(\d+) consumed=(\d+) " +
            "droppedQueueFull=(\d+) droppedSinkUnavailable=(\d+) discardedAfterSinkFailure=(\d+) sinkFailed=([01])"
        )
        if (-not $diagnosticsMatch.Success -or $diagnosticsMatch.Groups[1].Value -ne $mode) {
            throw "Diagnostics evidence missing: mode=$mode run=$runIndex`n$output"
        }

        $attempted = $diagnosticsMatch.Groups[2].Value
        $enqueued = $diagnosticsMatch.Groups[3].Value
        $consumed = $diagnosticsMatch.Groups[4].Value
        $droppedQueueFull = $diagnosticsMatch.Groups[5].Value
        $droppedSinkUnavailable = $diagnosticsMatch.Groups[6].Value
        $discardedAfterSinkFailure = $diagnosticsMatch.Groups[7].Value
        $sinkFailed = $diagnosticsMatch.Groups[8].Value
        [string]$currentCounterSignature =
            "$attempted|$enqueued|$consumed|$droppedQueueFull|$droppedSinkUnavailable|$discardedAfterSinkFailure|$sinkFailed"
        if ($counterSignature.Length -eq 0) {
            $counterSignature = $currentCounterSignature
        }
        elseif ($counterSignature -ne $currentCounterSignature) {
            throw "Diagnostics counters changed between runs: mode=$mode expected=$counterSignature actual=$currentCounterSignature"
        }

        if ($mode -eq "benchmark") {
            [System.Text.RegularExpressions.Match]$artifactMatch = [regex]::Match(
                $output,
                "benchmark artifact evidence: path=.+ bytes=(\d+) events=(\d+)"
            )
            if (-not $artifactMatch.Success -or $artifactMatch.Groups[2].Value -ne $consumed) {
                throw "Benchmark artifact evidence missing or inconsistent: run=$runIndex`n$output"
            }
            $artifactBytes.Add([double]::Parse($artifactMatch.Groups[1].Value))
        }

        $elapsedMilliseconds.Add($stopwatch.Elapsed.TotalMilliseconds)
    }

    [double]$elapsedMedian = Get-Median -Values $elapsedMilliseconds.ToArray()
    [double]$elapsedP95 = Get-NearestRankPercentile -Values $elapsedMilliseconds.ToArray() -Percentile 0.95
    [string]$artifactEvidence = "artifactBytesMin=0 artifactBytesMax=0"
    if ($artifactBytes.Count -ne 0) {
        [double]$artifactBytesMin = ($artifactBytes | Measure-Object -Minimum).Minimum
        [double]$artifactBytesMax = ($artifactBytes | Measure-Object -Maximum).Maximum
        $artifactEvidence = "artifactBytesMin=$artifactBytesMin artifactBytesMax=$artifactBytesMax"
    }

    [string]$elapsedMedianText = $elapsedMedian.ToString("F3", [Globalization.CultureInfo]::InvariantCulture)
    [string]$elapsedP95Text = $elapsedP95.ToString("F3", [Globalization.CultureInfo]::InvariantCulture)
    Write-Output (
        "diagnostics benchmark evidence: mode=$mode runs=$RepeatCount " +
        "elapsedMedianMs=$elapsedMedianText elapsedP95Ms=$elapsedP95Text " +
        "attempted=$attempted enqueued=$enqueued consumed=$consumed " +
        "droppedQueueFull=$droppedQueueFull droppedSinkUnavailable=$droppedSinkUnavailable " +
        "discardedAfterSinkFailure=$discardedAfterSinkFailure sinkFailed=$sinkFailed $artifactEvidence"
    )
}

Write-Output "artifacts: $runArtifactDirectory"
