param(
    [Parameter(Position = 0)]
    [ValidateSet("Server", "Client", "All")]
    [string]$Target = "All",
    [Parameter(Position = 1)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [ValidateSet("x64")]
    [string]$Platform = "x64"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

[string]$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
[string]$solutionDirectory = $repositoryRoot.TrimEnd('\') + '/'

function Resolve-MSBuildExecutable {
    $msbuildCommand = Get-Command "msbuild" -ErrorAction SilentlyContinue
    if ($null -ne $msbuildCommand -and -not [string]::IsNullOrWhiteSpace($msbuildCommand.Source)) {
        return $msbuildCommand.Source
    }

    [string]$vswherePath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswherePath -PathType Leaf)) {
        throw "MSBuild was not found. Install Visual Studio with Desktop development with C++."
    }

    [string[]]$matches = @(
        & $vswherePath `
            -latest `
            -products "*" `
            -requires Microsoft.Component.MSBuild `
            -find "MSBuild\**\Bin\MSBuild.exe"
    )
    if ($LASTEXITCODE -ne 0 -or $matches.Count -eq 0) {
        throw "Visual Studio MSBuild was not found through vswhere.exe."
    }

    return $matches[0]
}

function Invoke-NativeBuild {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath
    )

    Write-Output "Native build: $ProjectPath"
    & $script:msbuildPath `
        $ProjectPath `
        /m `
        "/p:SolutionDir=$solutionDirectory" `
        "/p:Configuration=$Configuration" `
        "/p:Platform=$Platform" `
        /verbosity:minimal
    if ($LASTEXITCODE -ne 0) {
        throw "Native build failed. project=$ProjectPath exitCode=$LASTEXITCODE"
    }
}

function Invoke-ClientBuild {
    [string]$clientProject = Join-Path $repositoryRoot "src\PrivateServer.GameClient\PrivateServer.GameClient.csproj"
    Write-Output "Managed client build: $clientProject"
    & dotnet build $clientProject --configuration $Configuration --nologo
    if ($LASTEXITCODE -ne 0) {
        throw "GameClient build failed. exitCode=$LASTEXITCODE"
    }
}

[string]$msbuildPath = Resolve-MSBuildExecutable
[string]$solutionPath = Join-Path $repositoryRoot "PrivateServer.sln"
[string]$serverProject = Join-Path $repositoryRoot "src\PrivateServer.WorldServer.Host\PrivateServer.WorldServer.Host.vcxproj"
[string]$clientNativeProject = Join-Path $repositoryRoot "src\PrivateServer.NetworkRuntime.CAbi\PrivateServer.NetworkRuntime.CAbi.vcxproj"

Write-Output "Build target: $Target"
Write-Output "Build configuration: $Configuration|$Platform"
Write-Output "MSBuild: $msbuildPath"

Write-Output "NuGet restore: $solutionPath"
& $msbuildPath `
    $solutionPath `
    /t:Restore `
    /p:RestorePackagesConfig=true `
    "/p:Configuration=$Configuration" `
    "/p:Platform=$Platform" `
    /verbosity:minimal
if ($LASTEXITCODE -ne 0) {
    throw "Solution NuGet restore failed. exitCode=$LASTEXITCODE"
}

if ($Target -eq "Server" -or $Target -eq "All") {
    Invoke-NativeBuild -ProjectPath $serverProject
}
if ($Target -eq "Client" -or $Target -eq "All") {
    Invoke-NativeBuild -ProjectPath $clientNativeProject
    Invoke-ClientBuild
}

Write-Output "Build completed: target=$Target configuration=$Configuration platform=$Platform"
