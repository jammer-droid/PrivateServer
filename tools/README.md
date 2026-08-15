# Tools 사용 가이드

이 문서는 로컬 개발환경 준비, 빌드, World Host와 Godot client 실행, benchmark와 artifact 위치를 설명한다. 명령은 저장소 루트에서 실행한다.

## 개발환경

공통 기준은 Windows x64다.

- Visual Studio 2026: Desktop development with C++, Windows SDK, MSBuild, NuGet, `v145`
- Visual Studio 2022 호환: 같은 workload와 `v143`
- .NET SDK 8.0 이상
- Godot .NET 4.7.1 x64
- Windows PowerShell 5.1 이상 또는 PowerShell 7 이상

일반 Godot 배포판은 C#을 지원하지 않으므로 반드시 `.NET` 배포판을 사용한다. `tools/build.ps1`은 PATH의 MSBuild를 먼저 찾고, 없으면 Visual Studio Installer의 `vswhere.exe`로 최신 MSBuild를 찾는다.

설치 확인:

```powershell
dotnet --info
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
  -latest -products "*" -requires Microsoft.Component.MSBuild
```

Visual Studio 2022에서는 저장소 루트에 Git 제외 파일 `LocalToolset.props`를 만들어 `v143`을 선택한다.

```xml
<Project>
  <PropertyGroup>
    <LocalPlatformToolset>v143</LocalPlatformToolset>
  </PropertyGroup>
</Project>
```

NuGet의 `nlohmann.json`은 solution restore로 준비된다. `spdlog`는 `src/vendor/spdlog`에 vendoring되어 별도 설치가 필요 없다.

## 빌드

일상적인 Host와 Game Client 개발 빌드는 `build.ps1`을 사용한다. 기본값은 `All`, `Debug`, `x64`다.

```powershell
# Server와 Client 전체 Debug
.\tools\build.ps1

# Server만
.\tools\build.ps1 -Target Server -Configuration Debug
.\tools\build.ps1 -Target Server -Configuration Release

# Game Client와 native dependency
.\tools\build.ps1 -Target Client -Configuration Debug
.\tools\build.ps1 -Target Client -Configuration Release

# Server와 Client 전체 Release
.\tools\build.ps1 -Target All -Configuration Release
```

`Server` target은 World Host와 의존 native library를, `Client` target은 CAbi/Runtime DLL, Managed adapter와 Godot C# assembly를 빌드한다. Debug/Release native DLL과 managed client 구성을 섞지 않는다.

모든 C++ test, smoke와 benchmark project를 포함한 전체 solution 빌드는 MSBuild를 직접 사용한다. Game Client는 solution 밖에 있으므로 필요하면 별도로 빌드한다.

```powershell
msbuild .\PrivateServer.sln /restore /m `
  /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64

dotnet build .\src\PrivateServer.GameClient\PrivateServer.GameClient.csproj `
  --configuration Debug
```

주요 산출물:

```text
build\bin\NetworkRuntime\x64\<Configuration>\
build\bin\WorldServer\x64\<Configuration>\
build\include\PrivateServer\NetworkRuntime\
```

Host와 WorldServer test를 빌드하면 같은 구성의 `PrivateServer.NetworkRuntime.dll`이 WorldServer 출력 폴더로 자동 복사된다.

## 단일 Host와 Game Client 실행

실행 스크립트는 빌드를 수행하지 않는다.

```powershell
# baseline: Host만 실행, 2명부터 round 시작
.\tools\run-world-host.ps1 0

# solo debug: Host와 Godot client 실행, 1명부터 round 시작
.\tools\run-world-host.ps1 1

# Release 선택
.\tools\run-world-host.ps1 0 -Configuration Release
```

Godot 실행 파일은 `-GodotExecutablePath`, `GODOT_EXECUTABLE`, PATH의 `godot`/`godot4`, 기본 개발 경로 순서로 찾는다.

```powershell
.\tools\run-world-host.ps1 1 `
  -GodotExecutablePath "C:\tools\Godot_v4.7.1-stable_mono_win64.exe"
```

클라이언트 채널 목록은 `src/PrivateServer.GameClient/Config/channels.local.json`이다. 개발 시 `res://Config/channels.local.json`으로 읽고 export 시 PCK에 포함되므로 exe 옆에 별도 JSON을 둘 필요가 없다.

## 다중 Channel Host 실행

`run-world-fleet.ps1`은 `config/world-host-fleet.json`에 등록된 Host를 함께 실행한다.

```powershell
.\tools\build.ps1 -Target Server -Configuration Debug
.\tools\run-world-fleet.ps1 -ValidateOnly
.\tools\run-world-fleet.ps1 -Configuration Debug
```

서버 전용 fleet manifest의 Host config 경로는 manifest 파일 위치 기준이다. launcher는 schema, 중복 channel ID·endpoint와 사용 중인 port를 시작 전에 검사한다. `Ctrl+C`는 살아 있는 Host PID 전체를 종료하며, Host 하나가 먼저 종료되어도 나머지 채널은 계속 실행한다.

기본 endpoint:

| Channel | Endpoint | Host config |
| --- | --- | --- |
| Channel 1 | `127.0.0.1:27015` | `config/world-server-baseline.json` |
| Channel 2 | `127.0.0.1:27016` | `config/world-server-channel-2.json` |

## Benchmark

Benchmark는 Release/x64 전체 solution 빌드를 사용한다. 실행 중인 일반 fleet과 endpoint를 공유할 수 없으므로 함께 실행하지 않는다.

단일 Channel canonical baseline:

```powershell
.\tools\run-world-host-benchmark-baseline.ps1
```

Channel 두 개에 각각 native gameplay client 100개를 연결하는 동시 부하 검증:

```powershell
.\tools\run-world-host-benchmark-fleet.ps1
```

Fleet benchmark는 controller 두 개가 각자 World Host를 시작하고 한 라운드가 끝나면 종료한다. 기본 라운드는 60Hz에서 10,800 tick으로 약 180초다. `-RepeatCount`는 같은 Host의 연속 라운드가 아니라 독립된 Host/run을 반복 생성한다.

```powershell
.\tools\run-world-host-benchmark-fleet.ps1 -RepeatCount 3
```

## Artifact

일반 Host 실행:

```text
artifacts\runs\<run-id>\
```

100×2 fleet benchmark:

```text
artifacts\fleet-benchmark\channel-1\<run-id>\
artifacts\fleet-benchmark\channel-2\<run-id>\
```

각 benchmark run의 최종 판정은 `benchmark/merged.json`에서 `completeness.complete`와 `verdict.valid`로 확인한다.

## 문제 해결

- Host가 즉시 종료되면 console 메시지와 `world/application.jsonl`을 확인한다.
- `0xC0000135`는 동일 구성의 Runtime DLL이 WorldServer 출력 폴더에 없는 경우가 많다. 같은 Platform/Configuration으로 다시 빌드한다.
- endpoint가 사용 중이면 `Get-NetTCPConnection -LocalPort <port> -State Listen`으로 소유 PID를 확인한다.
- Godot client가 native DLL을 찾지 못하면 먼저 같은 구성으로 `build.ps1 -Target Client`를 실행한다.
- NuGet restore가 실패하면 Visual Studio에서 `PrivateServer.sln` solution restore를 한 뒤 다시 실행한다.
