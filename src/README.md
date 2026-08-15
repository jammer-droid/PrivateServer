# src 프로젝트 온보딩

이 디렉터리는 C++/IOCP `NetworkRuntime` 관련 Visual Studio 프로젝트를 담는다.
중심 프로젝트는 `PrivateServer.NetworkRuntime`이고, 다른 프로젝트는 runtime을
수동 또는 자동으로 확인하기 위해 붙어 있다.

## 프로젝트 구성

| 프로젝트 | 타입 | 용도 |
| --- | --- | --- |
| `PrivateServer.NetworkRuntime` | DLL | Windows-native NetworkRuntime 구현 |
| `PrivateServer.NetworkRuntime.Internal` | Static library | DLL과 internal test가 공유하는 비공개 구현 |
| `PrivateServer.NetworkRuntime.InternalTests` | Google Test console app | Internal.lib를 직접 검증하는 테스트 프로그램 |
| `PrivateServer.NetworkRuntime.PublicTests` | Google Test console app | staged public header와 DLL import library만 사용하는 public behavior test |
| `PrivateServer.NetworkRuntime.Smoke` | Console app | 실제 loopback runtime lifecycle과 I/O 동작을 확인하는 프로그램 |

InternalTests는 구현 seam을, PublicTests는 제품 interface를 검증한다. Smoke는 실제 accept/recv/send/close/shutdown을 묶은 실행 증거라 PublicTests와 분리해 유지한다.

## 개발 환경

기본 개발 환경은 Windows + Visual Studio solution이다.

필요한 구성:

- Visual Studio 2026/18 계열
- MSVC C++ toolchain
- Windows SDK
- `v145` platform toolset
- Google Test project/template 지원

현재 Google Test는 Visual Studio Google Test template이 생성한 NuGet package
설정을 사용한다. 이 repo는 Google Test를 위해 `vcpkg.json`을 사용하지 않는다.

## 기본 빌드 방법

Solution:

```text
PrivateServer.sln
```

기본 작업 대상:

```text
Configuration: Debug 또는 Release
Platform: x64
```

Win32 설정은 Visual Studio 기본 생성값으로 남아 있을 수 있지만, 현재
NetworkRuntime 작업 기준은 x64다.

빌드 산출물 위치:

```text
$(SolutionDir)build\bin\NetworkRuntime\$(Platform)\$(Configuration)\
$(SolutionDir)build\obj\NetworkRuntime\$(MSBuildProjectName)\$(Platform)\$(Configuration)\
```

NetworkRuntime DLL, import library, smoke executable, test executable은 같은
`NetworkRuntime` artifact group 아래에 생성되도록 맞춘다. World Server 산출물은
별도 `WorldServer` artifact group을 사용한다.

DLL build는 public header whitelist를 다음 generated SDK 경로에 복사한다.

```text
$(SolutionDir)build\include\PrivateServer\NetworkRuntime\
```

## 프로젝트 연결 방식

Project dependency는 다음과 같다.

```text
PrivateServer.NetworkRuntime.Internal
  -> PrivateServer.NetworkRuntime
       -> PrivateServer.NetworkRuntime.PublicTests
       -> PrivateServer.NetworkRuntime.Smoke

PrivateServer.NetworkRuntime.Internal
  -> PrivateServer.NetworkRuntime.InternalTests
```

필요한 기본 연결:

- `PrivateServer.NetworkRuntime` project reference
- runtime header include path
- runtime import library link path
- `PrivateServer.NetworkRuntime.lib` linker dependency

대표 설정:

```text
C/C++ > General > Additional Include Directories (PublicTests/Smoke):
$(SolutionDir)build\include;%(AdditionalIncludeDirectories)

Linker > General > Additional Library Directories:
$(SolutionDir)build\bin\NetworkRuntime\$(Platform)\$(Configuration)\;%(AdditionalLibraryDirectories)

Linker > Input > Additional Dependencies:
PrivateServer.NetworkRuntime.lib;%(AdditionalDependencies)
```

실행 시에는 `PrivateServer.NetworkRuntime.dll`과 실행 파일이 같은 output directory에
있어야 한다.

## Google Test 의존성

`PrivateServer.NetworkRuntime.InternalTests`와 `PrivateServer.NetworkRuntime.PublicTests`는 Google Test NuGet package를 사용한다.

추적해야 하는 파일:

```text
src\PrivateServer.NetworkRuntime.InternalTests\packages.config
src\PrivateServer.NetworkRuntime.InternalTests\PrivateServer.NetworkRuntime.InternalTests.vcxproj
src\PrivateServer.NetworkRuntime.PublicTests\packages.config
src\PrivateServer.NetworkRuntime.PublicTests\PrivateServer.NetworkRuntime.PublicTests.vcxproj
```

추적하지 않는 파일:

```text
packages\
```

루트의 `packages\` directory는 NuGet restore 산출물이다. `.gitignore`에 두고
source처럼 commit하지 않는다.

## 빠른 확인 절차

1. `PrivateServer.sln`을 Visual Studio에서 연다.
2. `Debug | x64` 또는 `Release | x64`를 선택한다.
3. `PrivateServer.NetworkRuntime`을 빌드한다.
4. `PrivateServer.NetworkRuntime.InternalTests`와 `PrivateServer.NetworkRuntime.PublicTests`를 빌드하고 실행한다.
5. 통합 runtime evidence가 필요하면 `PrivateServer.NetworkRuntime.Smoke`를 실행한다.

## 주의사항

- 새 dependency는 바로 추가하지 말고 필요성과 대체안을 먼저 정리한다.
- Google Test는 현재 NuGet package 방식으로만 사용한다.
- vcpkg, CMake, 다른 package system은 현재 기본 개발 경로가 아니다.
- 공개 DLL API는 `docs/design/conventions/dll-boundary.md`의 노출 형태 선택 순서를 따른다.
- public header에는 구현 storage, Win32/Winsock type, internal virtual interface를 노출하지 않는다.
- Visual Studio filter는 논리 분류이고, public compile isolation은 generated `build/include` whitelist가 강제한다.
