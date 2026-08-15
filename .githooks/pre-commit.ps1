$ErrorActionPreference = 'Stop'

function Find-ClangFormat {
    $pathCommand = Get-Command 'clang-format.exe' -ErrorAction SilentlyContinue
    if ($null -ne $pathCommand) {
        return $pathCommand.Source
    }

    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswherePath)) {
        return $null
    }

    $installationPath = & $vswherePath -latest -products * -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installationPath)) {
        return $null
    }

    $candidatePaths = @(
        (Join-Path $installationPath 'VC\Tools\Llvm\x64\bin\clang-format.exe'),
        (Join-Path $installationPath 'VC\Tools\Llvm\bin\clang-format.exe')
    )

    foreach ($candidatePath in $candidatePaths) {
        if (Test-Path -LiteralPath $candidatePath) {
            return $candidatePath
        }
    }

    return $null
}

$repositoryRoot = & git rev-parse --show-toplevel
if ($LASTEXITCODE -ne 0) {
    Write-Error '저장소 루트를 확인할 수 없습니다.'
    exit 1
}

$stagedFiles = @(
    & git diff --cached --name-only --diff-filter=ACMR -- '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx'
)
if ($LASTEXITCODE -ne 0) {
    Write-Error 'staged C/C++ 파일 목록을 확인할 수 없습니다.'
    exit 1
}
if ($stagedFiles.Count -eq 0) {
    exit 0
}

$clangFormatPath = Find-ClangFormat
if ($null -eq $clangFormatPath) {
    Write-Error 'clang-format.exe를 찾을 수 없습니다. PATH 또는 Visual Studio LLVM 도구 설치를 확인하세요.'
    exit 1
}

Push-Location $repositoryRoot
try {
    foreach ($stagedFile in $stagedFiles) {
        & git diff --quiet -- $stagedFile
        $diffExitCode = $LASTEXITCODE
        if ($diffExitCode -eq 1) {
            Write-Error "stage하지 않은 변경이 있는 파일은 자동 포맷할 수 없습니다: $stagedFile"
            exit 1
        }
        if ($diffExitCode -ne 0) {
            Write-Error "파일 변경 상태를 확인할 수 없습니다: $stagedFile"
            exit 1
        }

        & $clangFormatPath --style=file -i -- $stagedFile
        if ($LASTEXITCODE -ne 0) {
            Write-Error "clang-format 적용에 실패했습니다: $stagedFile"
            exit 1
        }

        & git add -- $stagedFile
        if ($LASTEXITCODE -ne 0) {
            Write-Error "포맷된 파일을 stage하지 못했습니다: $stagedFile"
            exit 1
        }
    }
}
finally {
    Pop-Location
}

Write-Host "clang-format applied to $($stagedFiles.Count) staged C/C++ file(s)."
