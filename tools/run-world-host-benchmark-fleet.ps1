param(
    [string]$ExecutablePath = (
        Join-Path $PSScriptRoot "..\build\bin\NetworkRuntime\x64\Release\PrivateServer.NetworkRuntime.Benchmark.exe"
    ),
    [string]$Channel1ConfigPath = (
        Join-Path $PSScriptRoot "..\config\world-host-benchmark-channel-1.json"
    ),
    [string]$Channel2ConfigPath = (
        Join-Path $PSScriptRoot "..\config\world-host-benchmark-channel-2.json"
    ),
    [ValidateRange(1, 10)]
    [int]$RepeatCount = 1
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

[string]$singleChannelScript =
    (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "run-world-host-benchmark-baseline.ps1")).Path
[string]$resolvedExecutablePath = (Resolve-Path -LiteralPath $ExecutablePath).Path
[string[]]$resolvedConfigPaths = @(
    (Resolve-Path -LiteralPath $Channel1ConfigPath).Path,
    (Resolve-Path -LiteralPath $Channel2ConfigPath).Path
)
[System.Collections.Generic.HashSet[string]]$endpoints =
    [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
[System.Collections.Generic.HashSet[string]]$runsRoots =
    [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($configPath in $resolvedConfigPaths) {
    [object]$config = Get-Content -Raw -Encoding utf8 -LiteralPath $configPath | ConvertFrom-Json
    if ($config.schema -ne "psnr.world_server.benchmark.controller.config" -or
        $config.version -ne 1 -or
        $config.clients.count -ne 100) {
        throw "Fleet benchmark requires a version 1 World benchmark config with 100 clients. path=$configPath"
    }

    [string]$endpoint = "$($config.clients.address):$($config.clients.port)"
    if (-not $endpoints.Add($endpoint)) {
        throw "Fleet benchmark channel endpoints must be unique. endpoint=$endpoint"
    }

    [string]$configDirectory = Split-Path -Parent $configPath
    [string]$runsRoot = [System.IO.Path]::GetFullPath(
        (Join-Path $configDirectory ([string]$config.artifact.runsRoot)))
    if (-not $runsRoots.Add($runsRoot)) {
        throw "Fleet benchmark artifact roots must be unique. runsRoot=$runsRoot"
    }

    [System.Net.IPAddress]$address = [System.Net.IPAddress]::Parse([string]$config.clients.address)
    [System.Net.Sockets.TcpListener]$listener =
        [System.Net.Sockets.TcpListener]::new($address, [int]$config.clients.port)
    $listener.Server.ExclusiveAddressUse = $true
    try {
        $listener.Start()
    }
    catch [System.Net.Sockets.SocketException] {
        throw "Fleet benchmark endpoint is already in use. endpoint=$endpoint"
    }
    finally {
        $listener.Stop()
    }
}

[System.Collections.Generic.List[System.Management.Automation.Job]]$jobs =
    [System.Collections.Generic.List[System.Management.Automation.Job]]::new()
try {
    for ([int]$channelIndex = 0; $channelIndex -lt $resolvedConfigPaths.Length; ++$channelIndex) {
        [int]$channelId = $channelIndex + 1
        [System.Management.Automation.Job]$job = Start-Job -Name "world-benchmark-channel-$channelId" -ScriptBlock {
            param(
                [string]$ScriptPath,
                [string]$BenchmarkExecutablePath,
                [string]$ConfigPath,
                [int]$RunRepeatCount
            )

            & $ScriptPath `
                -ExecutablePath $BenchmarkExecutablePath `
                -ConfigPath $ConfigPath `
                -RepeatCount $RunRepeatCount
        } -ArgumentList @(
            $singleChannelScript,
            $resolvedExecutablePath,
            $resolvedConfigPaths[$channelIndex],
            $RepeatCount
        )
        $jobs.Add($job)
        Write-Output (
            "Fleet benchmark started: channelId=$channelId " +
            "config=$($resolvedConfigPaths[$channelIndex]) jobId=$($job.Id)"
        )
    }

    Wait-Job -Job $jobs.ToArray() | Out-Null
    [System.Collections.Generic.List[string]]$failures =
        [System.Collections.Generic.List[string]]::new()
    foreach ($job in $jobs) {
        Write-Output "----- $($job.Name) -----"
        Receive-Job -Job $job -ErrorAction Continue
        if ($job.State -ne [System.Management.Automation.JobState]::Completed) {
            [string]$reason = if ($null -eq $job.ChildJobs[0].JobStateInfo.Reason) {
                "unknown failure"
            } else {
                $job.ChildJobs[0].JobStateInfo.Reason.Message
            }
            $failures.Add("$($job.Name): $reason")
        }
    }

    if ($failures.Count -gt 0) {
        throw "Fleet benchmark failed: $($failures -join '; ')"
    }
    Write-Output "Fleet benchmark completed: channels=2 clientsPerChannel=100 totalClients=200"
}
finally {
    foreach ($job in $jobs) {
        if ($job.State -eq [System.Management.Automation.JobState]::Running) {
            Stop-Job -Job $job
        }
        Remove-Job -Job $job -Force
    }
}
