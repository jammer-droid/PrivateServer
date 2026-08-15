param(
    [string]$SolutionPath = "PrivateServer.sln",
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [string]$OutputPath = "compile_commands.json"
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath([string]$Path) {
    $repoRoot = Join-Path $PSScriptRoot ".."
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

function Normalize-JsonPath([string]$Path) {
    return ([System.IO.Path]::GetFullPath($Path)).Replace("\", "/")
}

function Split-MsBuildList([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return @()
    }

    return $Value.Split(";") |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ -ne "" -and $_ -notmatch "^%\(" }
}

function Split-OptionString([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return @()
    }

    return $Value.Split(" ", [System.StringSplitOptions]::RemoveEmptyEntries) |
        Where-Object { $_ -notmatch "^%\(" }
}

function Expand-ProjectValue(
    [string]$Value,
    [string]$SolutionDir,
    [string]$ProjectDir,
    [string]$ProjectName,
    [string]$Configuration,
    [string]$Platform
) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $Value
    }

    $expanded = $Value
    $expanded = $expanded.Replace('$(SolutionDir)', $SolutionDir)
    $expanded = $expanded.Replace('$(ProjectDir)', $ProjectDir)
    $expanded = $expanded.Replace('$(MSBuildProjectName)', $ProjectName)
    $expanded = $expanded.Replace('$(Configuration)', $Configuration)
    $expanded = $expanded.Replace('$(Platform)', $Platform)
    return $expanded
}

function Convert-IncludePath(
    [string]$Value,
    [string]$SolutionDir,
    [string]$ProjectDir,
    [string]$ProjectName,
    [string]$Configuration,
    [string]$Platform
) {
    $expanded = Expand-ProjectValue $Value $SolutionDir $ProjectDir $ProjectName $Configuration $Platform

    if ($expanded -match "\$\(") {
        return $null
    }

    if ([System.IO.Path]::IsPathRooted($expanded)) {
        return [System.IO.Path]::GetFullPath($expanded)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $ProjectDir $expanded))
}

function Get-ClangClPath {
    $pathCommand = Get-Command clang-cl.exe -ErrorAction SilentlyContinue
    if ($pathCommand) {
        return $pathCommand.Source
    }

    $zedClang = Get-ChildItem -LiteralPath (Join-Path $env:LOCALAPPDATA "Zed\languages\clangd") `
        -Recurse `
        -Filter "clang-cl.exe" `
        -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1

    if ($zedClang) {
        return $zedClang.FullName
    }

    return "clang-cl.exe"
}

function Test-ConditionMatches([string]$Condition, [string]$Configuration, [string]$Platform) {
    if ([string]::IsNullOrWhiteSpace($Condition)) {
        return $true
    }

    $needle = "'" + '$(Configuration)|$(Platform)' + "'=='$Configuration|$Platform'"
    return $Condition -eq $needle
}

function Get-XmlChildrenByName($Node, [string]$Name) {
    return @($Node.ChildNodes | Where-Object { $_.LocalName -eq $Name })
}

function Get-FirstXmlChildByName($Node, [string]$Name) {
    return Get-XmlChildrenByName $Node $Name | Select-Object -First 1
}

function Get-ProjectClCompileSettings($ProjectXml, [string]$Configuration, [string]$Platform) {
    $settings = @{
        Includes = New-Object System.Collections.Generic.List[string]
        Defines = New-Object System.Collections.Generic.List[string]
        Options = New-Object System.Collections.Generic.List[string]
        LanguageStandard = $null
        RuntimeLibrary = $null
    }

    $itemDefinitionGroups = @($ProjectXml.Project.ItemDefinitionGroup | Where-Object {
        Test-ConditionMatches $_.Condition $Configuration $Platform
    })

    foreach ($group in $itemDefinitionGroups) {
        $clCompile = Get-FirstXmlChildByName $group "ClCompile"
        if (-not $clCompile) {
            continue
        }

        $includeNode = Get-FirstXmlChildByName $clCompile "AdditionalIncludeDirectories"
        foreach ($include in Split-MsBuildList $includeNode.InnerText) {
            $settings.Includes.Add($include)
        }

        $defineNode = Get-FirstXmlChildByName $clCompile "PreprocessorDefinitions"
        foreach ($define in Split-MsBuildList $defineNode.InnerText) {
            $settings.Defines.Add($define)
        }

        $optionsNode = Get-FirstXmlChildByName $clCompile "AdditionalOptions"
        foreach ($option in Split-OptionString $optionsNode.InnerText) {
            $settings.Options.Add($option)
        }

        $languageStandardNode = Get-FirstXmlChildByName $clCompile "LanguageStandard"
        if ($languageStandardNode -and -not [string]::IsNullOrWhiteSpace($languageStandardNode.InnerText)) {
            $settings.LanguageStandard = $languageStandardNode.InnerText.Trim()
        }

        $runtimeLibraryNode = Get-FirstXmlChildByName $clCompile "RuntimeLibrary"
        if ($runtimeLibraryNode -and -not [string]::IsNullOrWhiteSpace($runtimeLibraryNode.InnerText)) {
            $settings.RuntimeLibrary = $runtimeLibraryNode.InnerText.Trim()
        }
    }

    return $settings
}

function Get-ProjectIncludePathProperties($ProjectXml, [string]$Configuration, [string]$Platform) {
    $includes = New-Object System.Collections.Generic.List[string]

    $propertyGroups = @($ProjectXml.Project.PropertyGroup | Where-Object {
        Test-ConditionMatches $_.Condition $Configuration $Platform
    })

    foreach ($group in $propertyGroups) {
        $includePathNode = Get-FirstXmlChildByName $group "IncludePath"
        foreach ($include in Split-MsBuildList $includePathNode.InnerText) {
            $includes.Add($include)
        }
    }

    return $includes
}

function Get-NuGetNativeIncludePaths([string]$ProjectDir, [string]$SolutionDir) {
    $paths = New-Object System.Collections.Generic.List[string]
    $packagesConfig = Join-Path $ProjectDir "packages.config"

    if (-not (Test-Path -LiteralPath $packagesConfig)) {
        return $paths
    }

    [xml]$packagesXml = Get-Content -LiteralPath $packagesConfig
    foreach ($package in $packagesXml.packages.package) {
        $packageDir = Join-Path $SolutionDir ("packages\{0}.{1}" -f $package.id, $package.version)
        $nativeInclude = Join-Path $packageDir "build\native\include"

        if (Test-Path -LiteralPath $nativeInclude) {
            $paths.Add([System.IO.Path]::GetFullPath($nativeInclude))
        }
    }

    return $paths
}

function Convert-LanguageStandard([string]$LanguageStandard) {
    switch ($LanguageStandard) {
        "stdcpp14" { return "/std:c++14" }
        "stdcpp17" { return "/std:c++17" }
        "stdcpp20" { return "/std:c++20" }
        "stdcpplatest" { return "/std:c++latest" }
        default { return $null }
    }
}

function Convert-RuntimeLibrary([string]$RuntimeLibrary) {
    switch ($RuntimeLibrary) {
        "MultiThreadedDebugDLL" { return "/MDd" }
        "MultiThreadedDLL" { return "/MD" }
        "MultiThreadedDebug" { return "/MTd" }
        "MultiThreaded" { return "/MT" }
        default { return $null }
    }
}

$solutionFullPath = Resolve-RepoPath $SolutionPath
$solutionDir = [System.IO.Path]::GetDirectoryName($solutionFullPath) + "\"
$outputFullPath = Resolve-RepoPath $OutputPath
$clangClPath = Get-ClangClPath

$projectPaths = Select-String -LiteralPath $solutionFullPath -Pattern 'Project\("\{[^"]+\}"\) = "[^"]+", "([^"]+\.vcxproj)"' |
    ForEach-Object {
        $relativePath = $_.Matches[0].Groups[1].Value
        [System.IO.Path]::GetFullPath((Join-Path $solutionDir $relativePath))
    }

$commands = New-Object System.Collections.Generic.List[object]

foreach ($projectPath in $projectPaths) {
    [xml]$projectXml = Get-Content -LiteralPath $projectPath
    $projectDir = [System.IO.Path]::GetDirectoryName($projectPath) + "\"
    $projectName = [System.IO.Path]::GetFileNameWithoutExtension($projectPath)

    $settings = Get-ProjectClCompileSettings $projectXml $Configuration $Platform
    foreach ($include in Get-ProjectIncludePathProperties $projectXml $Configuration $Platform) {
        $settings.Includes.Add($include)
    }
    foreach ($include in Get-NuGetNativeIncludePaths $projectDir $solutionDir) {
        $settings.Includes.Add($include)
    }

    $compileItems = @($projectXml.Project.ItemGroup.ClCompile | Where-Object {
        $_.Include -and $_.Include.Trim() -ne ""
    })

    foreach ($compileItem in $compileItems) {
        $sourcePath = [System.IO.Path]::GetFullPath((Join-Path $projectDir $compileItem.Include))

        $arguments = New-Object System.Collections.Generic.List[string]
        $arguments.Add($clangClPath)
        $arguments.Add("/TP")
        $arguments.Add("/nologo")

        $standardOption = Convert-LanguageStandard $settings.LanguageStandard
        if ($standardOption) {
            $arguments.Add($standardOption)
        }

        $runtimeOption = Convert-RuntimeLibrary $settings.RuntimeLibrary
        if ($runtimeOption) {
            $arguments.Add($runtimeOption)
        }

        foreach ($option in $settings.Options) {
            if ($option -ne "%(AdditionalOptions)") {
                $arguments.Add($option)
            }
        }

        foreach ($define in ($settings.Defines | Select-Object -Unique)) {
            if ($define -notmatch "^%\(") {
                $arguments.Add("/D$define")
            }
        }

        foreach ($include in ($settings.Includes | Select-Object -Unique)) {
            $includePath = Convert-IncludePath $include $solutionDir $projectDir $projectName $Configuration $Platform
            if ($includePath) {
                $arguments.Add("/I")
                $arguments.Add((Normalize-JsonPath $includePath))
            }
        }

        $arguments.Add((Normalize-JsonPath $sourcePath))

        $commands.Add([ordered]@{
            directory = Normalize-JsonPath $projectDir
            file = Normalize-JsonPath $sourcePath
            arguments = @($arguments)
        })
    }
}

$outputDirectory = [System.IO.Path]::GetDirectoryName($outputFullPath)
if (-not (Test-Path -LiteralPath $outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

$commands |
    ConvertTo-Json -Depth 10 |
    Set-Content -LiteralPath $outputFullPath -Encoding utf8

Write-Host ("Generated {0} entries: {1}" -f $commands.Count, $outputFullPath)
