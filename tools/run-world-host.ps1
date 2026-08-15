param(
    [Parameter(Position = 0)]
    [ValidateSet(0, 1)]
    [int]$Mode = 0,
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$ExecutablePath = "",
    [string]$GodotExecutablePath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

[string]$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
[string]$defaultExecutablePath = Join-Path $repositoryRoot "build\bin\WorldServer\x64\$Configuration\PrivateServer.WorldServer.Host.exe"
[string]$selectedExecutablePath = if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $defaultExecutablePath
} else {
    $ExecutablePath
}
[string]$configFileName = if ($Mode -eq 1) {
    "world-server-solo-debug.json"
} else {
    "world-server-baseline.json"
}
[string]$configPath = Join-Path $repositoryRoot "config\$configFileName"

[System.Management.Automation.PathInfo]$resolvedExecutable = Resolve-Path -LiteralPath $selectedExecutablePath
[System.Management.Automation.PathInfo]$resolvedConfig = Resolve-Path -LiteralPath $configPath
[string]$modeName = if ($Mode -eq 1) { "solo-debug" } else { "baseline" }

Write-Output "World Host mode: $modeName"
Write-Output "World Host configuration: $Configuration|x64"
Write-Output "World Host config: $($resolvedConfig.Path)"

if ($Mode -eq 0) {
    Push-Location $repositoryRoot
    try {
        & $resolvedExecutable.Path --config $resolvedConfig.Path
    }
    finally {
        Pop-Location
    }
    return
}

[System.Collections.Generic.List[string]]$godotCandidates = [System.Collections.Generic.List[string]]::new()
if (-not [string]::IsNullOrWhiteSpace($GodotExecutablePath)) {
    $godotCandidates.Add($GodotExecutablePath)
}
if (-not [string]::IsNullOrWhiteSpace($env:GODOT_EXECUTABLE)) {
    $godotCandidates.Add($env:GODOT_EXECUTABLE)
}
foreach ($commandName in @("godot", "godot4")) {
    [System.Management.Automation.CommandInfo]$command = Get-Command $commandName -ErrorAction SilentlyContinue
    if ($null -ne $command -and -not [string]::IsNullOrWhiteSpace($command.Source)) {
        $godotCandidates.Add($command.Source)
    }
}
$godotCandidates.Add((Join-Path $env:USERPROFILE "Desktop\Dev\Godot_v4.7.1-stable_mono_win64\Godot_v4.7.1-stable_mono_win64.exe"))

[string]$resolvedGodotPath = ""
foreach ($candidate in $godotCandidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $resolvedGodotPath = (Resolve-Path -LiteralPath $candidate).Path
        break
    }
}
if ([string]::IsNullOrWhiteSpace($resolvedGodotPath)) {
    throw "Godot executable was not found. Pass -GodotExecutablePath or set GODOT_EXECUTABLE."
}

[string]$gameClientPath = Join-Path $repositoryRoot "src\PrivateServer.GameClient"
Write-Output "Godot executable: $resolvedGodotPath"
Write-Output "Godot project: $gameClientPath"

[System.Diagnostics.Process]$hostProcess = Start-Process `
    -FilePath $resolvedExecutable.Path `
    -ArgumentList @("--config", "`"$($resolvedConfig.Path)`"") `
    -WorkingDirectory $repositoryRoot `
    -WindowStyle Hidden `
    -PassThru

try {
    if ($hostProcess.HasExited) {
        throw "World Host exited before the Godot client started. exitCode=$($hostProcess.ExitCode)"
    }

    [System.Diagnostics.Process]$godotProcess = Start-Process `
        -FilePath $resolvedGodotPath `
        -ArgumentList @("--path", "`"$gameClientPath`"") `
        -WorkingDirectory $repositoryRoot `
        -PassThru
    $godotProcess.WaitForExit()
}
finally {
    if (-not $hostProcess.HasExited) {
        Stop-Process -Id $hostProcess.Id
        $hostProcess.WaitForExit()
        Write-Output "World Host stopped after the Godot client exited. pid=$($hostProcess.Id)"
    }
}
