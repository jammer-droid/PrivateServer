param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$ManifestPath = "",
    [string]$ExecutablePath = "",
    [string]$RunsRoot = "",
    [ValidateRange(100, 10000)]
    [int]$StartupProbeMilliseconds = 1000,
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

[string]$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
[string]$defaultManifestPath = Join-Path $repositoryRoot "config\world-host-fleet.json"
[string]$selectedManifestPath = if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $defaultManifestPath
} else {
    $ManifestPath
}
[string]$defaultExecutablePath =
    Join-Path $repositoryRoot "build\bin\WorldServer\x64\$Configuration\PrivateServer.WorldServer.Host.exe"
[string]$selectedExecutablePath = if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $defaultExecutablePath
} else {
    $ExecutablePath
}

function Get-RequiredProperty {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Object,
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [string]$Context
    )

    [System.Management.Automation.PSPropertyInfo]$property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        throw "Missing required property '$Name'. context=$Context"
    }
    return $property.Value
}

function Resolve-ManifestRelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$ManifestDirectory
    )

    [string]$candidate = if ([System.IO.Path]::IsPathRooted($Path)) {
        $Path
    } else {
        Join-Path $ManifestDirectory $Path
    }
    return (Resolve-Path -LiteralPath $candidate).Path
}

function Test-EndpointAvailable {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Address,
        [Parameter(Mandatory = $true)]
        [int]$Port
    )

    [System.Net.IPAddress]$ipAddress = [System.Net.IPAddress]::Parse($Address)
    [System.Net.Sockets.TcpListener]$listener = [System.Net.Sockets.TcpListener]::new($ipAddress, $Port)
    $listener.Server.ExclusiveAddressUse = $true
    try {
        $listener.Start()
        return $true
    }
    catch [System.Net.Sockets.SocketException] {
        return $false
    }
    finally {
        $listener.Stop()
    }
}

[string]$resolvedManifestPath = (Resolve-Path -LiteralPath $selectedManifestPath).Path
[string]$manifestDirectory = Split-Path -Parent $resolvedManifestPath
[object]$manifest = Get-Content -Raw -LiteralPath $resolvedManifestPath | ConvertFrom-Json
[string]$manifestSchema = [string](Get-RequiredProperty $manifest "schema" "manifest")
[int]$manifestVersion = [int](Get-RequiredProperty $manifest "version" "manifest")
[object[]]$manifestHosts = @(Get-RequiredProperty $manifest "hosts" "manifest")
if ($manifestSchema -ne "psnr.world_server.host.fleet" -or $manifestVersion -ne 1 -or $manifestHosts.Count -eq 0) {
    throw "Unsupported or empty World Host fleet manifest. path=$resolvedManifestPath"
}

[System.Collections.Generic.HashSet[uint32]]$channelIds = [System.Collections.Generic.HashSet[uint32]]::new()
[System.Collections.Generic.HashSet[string]]$endpoints =
    [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
[System.Collections.Generic.List[object]]$channels = [System.Collections.Generic.List[object]]::new()
foreach ($entry in $manifestHosts) {
    [string]$hostConfig = [string](Get-RequiredProperty $entry "config" "fleet host")
    [string]$resolvedConfigPath = Resolve-ManifestRelativePath `
        -Path $hostConfig `
        -ManifestDirectory $manifestDirectory
    [object]$hostDocument = Get-Content -Raw -LiteralPath $resolvedConfigPath | ConvertFrom-Json
    [string]$hostSchema = [string](Get-RequiredProperty $hostDocument "schema" "host config $resolvedConfigPath")
    [int]$hostVersion = [int](Get-RequiredProperty $hostDocument "version" "host config $resolvedConfigPath")
    if ($hostSchema -ne "psnr.world_server.host.config" -or $hostVersion -ne 2) {
        throw "Unsupported Host config schema. config=$resolvedConfigPath"
    }
    [object]$hostChannel = Get-RequiredProperty $hostDocument "channel" "host config $resolvedConfigPath"
    [object]$hostNetwork = Get-RequiredProperty $hostDocument "network" "host config $resolvedConfigPath"
    [uint32]$hostChannelId = [uint32](Get-RequiredProperty $hostChannel "id" "host config channel")
    [string]$hostChannelName = [string](Get-RequiredProperty $hostChannel "name" "host config channel")
    [string]$hostAddress = [string](Get-RequiredProperty $hostNetwork "bindAddress" "host config network")
    [int]$hostPort = [int](Get-RequiredProperty $hostNetwork "port" "host config network")
    if ($hostChannelId -eq 0 -or
        [string]::IsNullOrWhiteSpace($hostChannelName) -or
        $hostPort -le 0 -or
        $hostPort -gt 65535) {
        throw "Invalid channel settings in Host config. config=$resolvedConfigPath"
    }
    [System.Net.IPAddress]$parsedAddress = $null
    if (-not [System.Net.IPAddress]::TryParse($hostAddress, [ref]$parsedAddress) -or
        $parsedAddress.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
        throw "Host bind address must be IPv4. channelId=$hostChannelId address=$hostAddress"
    }
    [string]$endpoint = "$hostAddress`:$hostPort"
    if (-not $channelIds.Add($hostChannelId)) {
        throw "Duplicate channel id. channelId=$hostChannelId"
    }
    if (-not $endpoints.Add($endpoint)) {
        throw "Duplicate channel endpoint. endpoint=$endpoint"
    }

    $channels.Add([pscustomobject]@{
        Id = $hostChannelId
        Name = $hostChannelName
        Address = $hostAddress
        Port = $hostPort
        Endpoint = $endpoint
        ConfigPath = $resolvedConfigPath
    })
}

Write-Output "World fleet manifest: $resolvedManifestPath"
foreach ($channel in $channels) {
    Write-Output "Validated channel: id=$($channel.Id) name=$($channel.Name) endpoint=$($channel.Endpoint) config=$($channel.ConfigPath)"
}
if ($ValidateOnly) {
    Write-Output "World fleet validation completed. channelCount=$($channels.Count)"
    return
}

[string]$resolvedExecutablePath = (Resolve-Path -LiteralPath $selectedExecutablePath).Path
[string]$resolvedRunsRoot = ""
if (-not [string]::IsNullOrWhiteSpace($RunsRoot)) {
    if (-not [System.IO.Path]::IsPathRooted($RunsRoot)) {
        throw "RunsRoot must be an absolute path. path=$RunsRoot"
    }
    $resolvedRunsRoot = [System.IO.Path]::GetFullPath($RunsRoot)
}
foreach ($channel in $channels) {
    if (-not (Test-EndpointAvailable -Address $channel.Address -Port $channel.Port)) {
        throw "Channel endpoint is already in use. channelId=$($channel.Id) endpoint=$($channel.Endpoint)"
    }
}

[System.Collections.Generic.List[object]]$started = [System.Collections.Generic.List[object]]::new()
try {
    foreach ($channel in $channels) {
        [System.Collections.Generic.List[string]]$arguments =
            [System.Collections.Generic.List[string]]::new()
        $arguments.Add("--config")
        $arguments.Add("`"$($channel.ConfigPath)`"")
        if (-not [string]::IsNullOrWhiteSpace($resolvedRunsRoot)) {
            $arguments.Add("--runs-root")
            $arguments.Add("`"$resolvedRunsRoot`"")
        }
        [System.Diagnostics.Process]$process = Start-Process `
            -FilePath $resolvedExecutablePath `
            -ArgumentList $arguments `
            -WorkingDirectory $repositoryRoot `
            -WindowStyle Hidden `
            -PassThru
        $started.Add([pscustomobject]@{ Channel = $channel; Process = $process })
        Write-Output "World Host starting: channelId=$($channel.Id) endpoint=$($channel.Endpoint) pid=$($process.Id)"
    }

    Start-Sleep -Milliseconds $StartupProbeMilliseconds
    Write-Output "World fleet running. channelCount=$($started.Count) Press Ctrl+C to stop."

    [System.Collections.Generic.HashSet[int]]$reportedExitedProcessIds =
        [System.Collections.Generic.HashSet[int]]::new()
    while ($true) {
        [int]$runningCount = 0
        foreach ($item in $started) {
            if ($item.Process.HasExited) {
                if ($reportedExitedProcessIds.Add($item.Process.Id)) {
                    Write-Error `
                        -Message "World Host exited. channelId=$($item.Channel.Id) exitCode=$($item.Process.ExitCode)" `
                        -ErrorAction Continue
                }
                continue
            }
            ++$runningCount
        }
        if ($runningCount -eq 0) {
            throw "All World Hosts have exited."
        }
        Start-Sleep -Milliseconds 250
    }
}
finally {
    [System.Collections.Generic.List[int]]$runningProcessIds =
        [System.Collections.Generic.List[int]]::new()
    foreach ($item in $started) {
        if (-not $item.Process.HasExited) {
            $runningProcessIds.Add($item.Process.Id)
        }
    }
    if ($runningProcessIds.Count -gt 0) {
        Stop-Process `
            -Id $runningProcessIds.ToArray() `
            -Force `
            -ErrorAction SilentlyContinue
    }
}
