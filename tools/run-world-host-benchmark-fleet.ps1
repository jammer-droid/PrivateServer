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
    [int]$RepeatCount = 1,
    [switch]$LaunchObservers,
    [string]$GodotExecutablePath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($LaunchObservers -and $RepeatCount -ne 1) {
    throw "Observer capture mode supports exactly one fleet benchmark round. repeatCount=$RepeatCount"
}

function Resolve-GodotExecutable {
    param(
        [string]$ExplicitPath
    )

    [System.Collections.Generic.List[string]]$candidates =
        [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        $candidates.Add($ExplicitPath)
    }
    if (-not [string]::IsNullOrWhiteSpace($env:GODOT_EXECUTABLE)) {
        $candidates.Add($env:GODOT_EXECUTABLE)
    }
    foreach ($commandName in @("godot", "godot4")) {
        [System.Management.Automation.CommandInfo]$command =
            Get-Command $commandName -ErrorAction SilentlyContinue
        if ($null -ne $command -and
            -not [string]::IsNullOrWhiteSpace($command.Source)) {
            $candidates.Add($command.Source)
        }
    }
    $candidates.Add((Join-Path $env:USERPROFILE "Desktop\Dev\Godot_v4.7.1-stable_mono_win64\Godot_v4.7.1-stable_mono_win64.exe"))

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Godot executable was not found. Pass -GodotExecutablePath or set GODOT_EXECUTABLE."
}

function Wait-FleetEndpointListening {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Address,
        [Parameter(Mandatory = $true)]
        [int]$Port
    )

    [System.Diagnostics.Stopwatch]$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    while ($stopwatch.ElapsedMilliseconds -lt 10000) {
        [System.Net.IPEndPoint[]]$listeners =
            [System.Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners()
        foreach ($listener in $listeners) {
            if ($listener.Address.ToString() -eq $Address -and $listener.Port -eq $Port) {
                return
            }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Fleet endpoint did not begin listening within 10 seconds. endpoint=$Address`:$Port"
}

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

[string]$resolvedGodotPath = ""
[string]$gameClientPath = ""
if ($LaunchObservers) {
    $resolvedGodotPath = Resolve-GodotExecutable -ExplicitPath $GodotExecutablePath
    $gameClientPath = (Resolve-Path -LiteralPath (
        Join-Path $PSScriptRoot "..\src\PrivateServer.GameClient"
    )).Path
    [string]$clientChannelDirectoryPath = Join-Path $gameClientPath "Config\channels.local.json"
    [object]$clientChannelDirectory =
        Get-Content -Raw -Encoding utf8 -LiteralPath $clientChannelDirectoryPath | ConvertFrom-Json
    if ($clientChannelDirectory.schema -ne "psnr.game_client.channels" -or
        $clientChannelDirectory.version -ne 1) {
        throw "Unsupported Game Client channel directory. path=$clientChannelDirectoryPath"
    }
    for ([int]$channelIndex = 0; $channelIndex -lt $resolvedConfigPaths.Length; ++$channelIndex) {
        [uint32]$channelId = [uint32]($channelIndex + 1)
        [object]$benchmarkConfig =
            Get-Content -Raw -Encoding utf8 -LiteralPath $resolvedConfigPaths[$channelIndex] | ConvertFrom-Json
        [object[]]$clientChannels = @(
            $clientChannelDirectory.channels | Where-Object { [uint32]$_.id -eq $channelId }
        )
        if ($clientChannels.Count -ne 1) {
            throw "Observer channel directory must contain one matching channel. channelId=$channelId"
        }
        [string]$clientAddress = [string]$clientChannels[0].address
        [int]$clientPort = [int]$clientChannels[0].port
        [string]$benchmarkAddress = [string]$benchmarkConfig.clients.address
        [int]$benchmarkPort = [int]$benchmarkConfig.clients.port
        if ($clientAddress -ne $benchmarkAddress -or $clientPort -ne $benchmarkPort) {
            throw "Observer channel directory does not match fleet benchmark endpoint. channelId=$channelId"
        }
    }
    Write-Output "Godot observer executable: $resolvedGodotPath"
    Write-Output "Godot observer project: $gameClientPath"
}

[System.Collections.Generic.List[System.Management.Automation.Job]]$jobs =
    [System.Collections.Generic.List[System.Management.Automation.Job]]::new()
[System.Collections.Generic.List[System.Diagnostics.Process]]$observerProcesses =
    [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
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

    if ($LaunchObservers) {
        for ([int]$channelIndex = 0; $channelIndex -lt $resolvedConfigPaths.Length; ++$channelIndex) {
            [object]$benchmarkConfig =
                Get-Content -Raw -Encoding utf8 -LiteralPath $resolvedConfigPaths[$channelIndex] | ConvertFrom-Json
            Wait-FleetEndpointListening `
                -Address ([string]$benchmarkConfig.clients.address) `
                -Port ([int]$benchmarkConfig.clients.port)
        }
        [int]$observerWindowWidth = 960
        [int]$observerWindowHeight = 600
        for ([int]$channelIndex = 0; $channelIndex -lt $resolvedConfigPaths.Length; ++$channelIndex) {
            [int]$channelId = $channelIndex + 1
            [int]$windowX = $channelIndex * $observerWindowWidth
            [string[]]$observerArguments = @(
                "--path",
                "`"$gameClientPath`"",
                "--windowed",
                "--resolution",
                "$observerWindowWidth`x$observerWindowHeight",
                "--position",
                "$windowX,0",
                "--",
                "--observe-channel",
                "$channelId"
            )
            [System.Diagnostics.Process]$observerProcess = Start-Process `
                -FilePath $resolvedGodotPath `
                -ArgumentList $observerArguments `
                -WorkingDirectory $gameClientPath `
                -PassThru
            $observerProcesses.Add($observerProcess)
            Write-Output (
                "Godot observer starting: channelId=$channelId " +
                "pid=$($observerProcess.Id) position=$windowX,0 " +
                "resolution=$observerWindowWidth`x$observerWindowHeight"
            )
        }
        Start-Sleep -Milliseconds 1000
        foreach ($observerProcess in $observerProcesses) {
            if ($observerProcess.HasExited) {
                throw "Godot observer exited during startup. pid=$($observerProcess.Id) exitCode=$($observerProcess.ExitCode)"
            }
        }
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
    if ($LaunchObservers) {
        Start-Sleep -Milliseconds 5000
    }
    Write-Output "Fleet benchmark completed: channels=2 clientsPerChannel=100 totalClients=200"
}
finally {
    foreach ($observerProcess in $observerProcesses) {
        if (-not $observerProcess.HasExited) {
            Stop-Process -Id $observerProcess.Id -ErrorAction SilentlyContinue
            $observerProcess.WaitForExit()
        }
    }
    foreach ($job in $jobs) {
        if ($job.State -eq [System.Management.Automation.JobState]::Running) {
            Stop-Job -Job $job
        }
        Remove-Job -Job $job -Force
    }
}
