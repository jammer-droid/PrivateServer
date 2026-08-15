#include "BenchmarkServerChildProcess.h"

#define NOMINMAX
#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace psnr::benchmark
{
    namespace
    {
        constexpr DWORD ForcedTerminationExitCode = 1;
        constexpr DWORD ForcedTerminationWaitMilliseconds = 10'000;     // 10 secs
        constexpr std::size_t MaximumExecutablePathCharacters = 32'768; // Windows maximum extended-length
        constexpr std::size_t MaximumRunIdBytes = 128;

        class Win32OwnedHandle final
        {
        public:
            Win32OwnedHandle() noexcept = default;

            explicit Win32OwnedHandle(const HANDLE handle) noexcept
                : handle_(handle)
            {
            }

            ~Win32OwnedHandle() noexcept
            {
                Reset();
            }

            Win32OwnedHandle(const Win32OwnedHandle&) = delete;
            Win32OwnedHandle& operator=(const Win32OwnedHandle&) = delete;

            [[nodiscard]] HANDLE Get() const noexcept
            {
                return handle_;
            }

            [[nodiscard]] HANDLE Release() noexcept
            {
                const HANDLE handle = handle_;
                handle_ = nullptr;
                return handle;
            }

            void Reset(const HANDLE handle = nullptr) noexcept
            {
                if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(handle_);
                }
                handle_ = handle;
            }

        private:
            HANDLE handle_ = nullptr;
        };

        // CreateProcessW에 전달할 확장 옵션 관리 클래스
        // Controller가 보유한 HANDLE 중에서 commandRead, eventWrite만 child가 상속하게 만듬
        class ProcessThreadAttributeList final
        {
        public:
            ProcessThreadAttributeList() noexcept = default;

            ~ProcessThreadAttributeList() noexcept
            {
                if (list_ != nullptr)
                {
                    DeleteProcThreadAttributeList(list_);
                }
            }

            ProcessThreadAttributeList(const ProcessThreadAttributeList&) = delete;
            ProcessThreadAttributeList& operator=(const ProcessThreadAttributeList&) = delete;

            // handle 상속을 위한 list_ 초기화
            // 필요 attribute 개수 1개
            [[nodiscard]] bool Initialize(std::uint32_t* const outNativeErrorCode)
            {
                SIZE_T requiredBytes = 0;
                // 등록할 attribute 개수 1개, 0으로 초기화, 필요한 메모리 크기 확인
                InitializeProcThreadAttributeList(nullptr, 1, 0, &requiredBytes);
                if (requiredBytes == 0)
                {
                    if (outNativeErrorCode != nullptr)
                    {
                        *outNativeErrorCode = GetLastError();
                    }
                    return false;
                }

                storage_.resize(requiredBytes); // for attribute list
                list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
                // 준비한 메모리로 attribute list 를 실제 초기화, 실패 시 GetLastError()로 오류 코드 확인 가능
                if (InitializeProcThreadAttributeList(list_, 1, 0, &requiredBytes) == FALSE)
                {
                    if (outNativeErrorCode != nullptr)
                    {
                        *outNativeErrorCode = GetLastError();
                    }
                    list_ = nullptr;
                    return false;
                }
                return true;
            }

            // 상속할 HANDLE 목록 등록
            [[nodiscard]] bool SetInheritedHandles(HANDLE* const handles, const std::size_t handleCount,
                                                   std::uint32_t* const outNativeErrorCode) noexcept
            {
                const SIZE_T handlesBytes = handleCount * sizeof(HANDLE);

                // PROC_THREAD_ATTRIBUTE_HANDLE_LIST -> handles 를 상속할 수 있게 whitelist 등록 대상으로 설정
                // list_ 에 handles 를 등록
                // list_
                // └─ attribute entry 1개
                // ├─ key : PROC_THREAD_ATTRIBUTE_HANDLE_LIST
                // ├─ value : inheritedHandles 배열의 포인터
                // └─ size : handleCount * sizeof(HANDLE)

                if (UpdateProcThreadAttribute(list_, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, handlesBytes,
                                              nullptr, nullptr) == FALSE)
                {
                    if (outNativeErrorCode != nullptr)
                    {
                        *outNativeErrorCode = GetLastError();
                    }
                    return false;
                }
                return true;
            }

            [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST Get() const noexcept
            {
                return list_;
            }

        private:
            std::vector<std::byte> storage_;
            LPPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
        };

        [[nodiscard]] BenchmarkServerChildLaunchResult LaunchFailure(std::string error,
                                                                     const std::uint32_t nativeErrorCode = 0)
        {
            BenchmarkServerChildLaunchResult result;
            result.error = std::move(error);
            result.nativeErrorCode = nativeErrorCode;
            return result;
        }

        [[nodiscard]] bool MakeNonInheritable(const HANDLE handle, std::uint32_t* const outNativeErrorCode) noexcept
        {
            if (SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0) != FALSE) // 상속 금지 설정
            {
                return true;
            }
            if (outNativeErrorCode != nullptr)
            {
                *outNativeErrorCode = GetLastError();
            }
            return false;
        }

        [[nodiscard]] bool Utf8ToWide(const std::string_view text, std::wstring* const outText,
                                      std::uint32_t* const outNativeErrorCode)
        {
            if (outText == nullptr || text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                return false;
            }

            if (text.empty())
            {
                outText->clear();
                return true;
            }

            const int sourceLength = static_cast<int>(text.size());
            // MultiByte 변환 길이 확인
            const int requiredCharacters =
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), sourceLength, nullptr, 0);
            if (requiredCharacters == 0)
            {
                if (outNativeErrorCode != nullptr)
                {
                    *outNativeErrorCode = GetLastError();
                }
                return false;
            }

            outText->resize(static_cast<std::size_t>(requiredCharacters));
            // 요구하는 길이만큼 UTF-8 -> MultiByte 변환
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), sourceLength, outText->data(),
                                    requiredCharacters) == 0)
            {
                if (outNativeErrorCode != nullptr)
                {
                    *outNativeErrorCode = GetLastError();
                }
                return false;
            }
            return true;
        }

        // CreateProcessW 함수는 argv[] 배열 대신, 하나의 문자열을 받음
        // 입력 문자열은 child process 의 CRT Parser에서 다시 argv[] 로 변환됨
        // 공백, 따옴표, 역슬래시를 Windows Quoting 규칙에 따라 처리해야 함(Windows 커맨드 라인 규칙을 따라감)
        // - 따옴표 앞에 연속된 역슬래시가 있으면 예외 처리
        // - 따옴표 앞에 2N  개 -> 역슬래시 N 개를 결과에 남김 + 인자를 여닫는 따옴표
        // - 따옴표 앞에 2N+1개 -> 역슬래시 N 개를 결과에 남김 + 따옴표 자체를 실제 문자로 남김(커맨드 라인 닫는 따옴표 아님)
        void AppendQuotedArgument(const std::wstring_view argument, std::wstring* const commandLine)
        {
            commandLine->push_back(L'"'); // Windows 커맨드라인 시작 따옴표 추가
            std::size_t backslashCount = 0;
            for (const wchar_t character : argument)
            {
                if (character == L'\\') // 문자열 내부 역슬래시 카운팅
                {
                    ++backslashCount;
                    continue;
                }

                if (character == L'"') // 커맨드라인 내부 따옴표 처리
                {
                    // 내부 따옴표는 인자의 일부로 취급해야 함
                    // 역슬래시가 앞에 N개 있으면 2N + 1개를 따옴표 앞에 남김(\\\")
                    // \\\" -> \" 로 파서가 처리함(경로 구분자 및 따옴표 보존)
                    commandLine->append(backslashCount * 2 + 1, L'\\');
                    commandLine->push_back(L'"');
                    backslashCount = 0;
                    continue;
                }

                commandLine->append(backslashCount, L'\\');
                backslashCount = 0;
                commandLine->push_back(character);
            }
            commandLine->append(backslashCount * 2, L'\\');
            commandLine->push_back(L'"'); // Windows 커맨드라인 종료 따옴표 추가
        }

        [[nodiscard]] bool CurrentExecutablePath(std::wstring* const outPath, std::uint32_t* const outNativeErrorCode)
        {
            if (outPath == nullptr)
            {
                return false;
            }

            std::vector<wchar_t> buffer(MaximumExecutablePathCharacters);
            // GetModuleFileNameW 의 첫 번째 인자가 NULL이면 현재 프로세스의 실행 경로를 가져옴
            const DWORD pathCharacters = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (pathCharacters == 0 || pathCharacters >= buffer.size())
            {
                if (outNativeErrorCode != nullptr)
                {
                    *outNativeErrorCode = GetLastError();
                }
                return false;
            }

            outPath->assign(buffer.data(), pathCharacters);
            return true;
        }

        [[nodiscard]] std::wstring BuildBenchmarkChildCommandLine(const std::wstring& executablePath,
                                                                  const HANDLE commandReadHandle,
                                                                  const HANDLE eventWriteHandle,
                                                                  const std::wstring& runId,
                                                                  const std::wstring& configPath)
        {
            std::wstring commandLine;
            AppendQuotedArgument(executablePath, &commandLine); // 실행 경로
            commandLine.append(L" --server-child --run-id ");
            AppendQuotedArgument(runId, &commandLine);
            commandLine.append(L" --command-pipe-handle ");
            commandLine.append(std::to_wstring(reinterpret_cast<std::uintptr_t>(commandReadHandle)));
            commandLine.append(L" --event-pipe-handle ");
            commandLine.append(std::to_wstring(reinterpret_cast<std::uintptr_t>(eventWriteHandle)));

            if (!configPath.empty())
            {
                commandLine.append(L" --config ");
                AppendQuotedArgument(configPath, &commandLine);
            }
            return commandLine;
        }

        [[nodiscard]] std::wstring BuildWorldHostCommandLine(const std::wstring& executablePath,
                                                             const HANDLE commandReadHandle,
                                                             const HANDLE eventWriteHandle, const std::wstring& runId,
                                                             const std::wstring& configPath,
                                                             const std::wstring& runsRoot)
        {
            std::wstring commandLine;
            AppendQuotedArgument(executablePath, &commandLine);
            commandLine.append(L" --run-id ");
            AppendQuotedArgument(runId, &commandLine);
            commandLine.append(L" --config ");
            AppendQuotedArgument(configPath, &commandLine);
            commandLine.append(L" --runs-root ");
            AppendQuotedArgument(runsRoot, &commandLine);
            commandLine.append(L" --command-pipe-handle ");
            commandLine.append(std::to_wstring(reinterpret_cast<std::uintptr_t>(commandReadHandle)));
            commandLine.append(L" --event-pipe-handle ");
            commandLine.append(std::to_wstring(reinterpret_cast<std::uintptr_t>(eventWriteHandle)));
            return commandLine;
        }

        void TerminateAndWait(const HANDLE processHandle) noexcept
        {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(processHandle, &exitCode) != FALSE && exitCode == STILL_ACTIVE)
            {
                if (TerminateProcess(processHandle, ForcedTerminationExitCode) != FALSE)
                {
                    static_cast<void>(WaitForSingleObject(processHandle, ForcedTerminationWaitMilliseconds));
                }
            }
        }
    } // namespace

    BenchmarkServerChildProcess::BenchmarkServerChildProcess(const BenchmarkNativeProcessHandle processHandle,
                                                             BenchmarkIpcOwnedHandle&& commandWriteHandle,
                                                             BenchmarkIpcOwnedHandle&& eventReadHandle) noexcept
        : processHandle_(processHandle)
        , commandWriter_(std::move(commandWriteHandle))
        , eventReader_(std::move(eventReadHandle))
    {
    }

    BenchmarkServerChildProcess::~BenchmarkServerChildProcess() noexcept
    {
        const HANDLE processHandle = static_cast<HANDLE>(processHandle_);
        if (processHandle != nullptr && processHandle != INVALID_HANDLE_VALUE)
        {
            TerminateAndWait(processHandle);
            CloseHandle(processHandle);
        }
        processHandle_ = nullptr;
    }

    BenchmarkServerChildLaunchResult BenchmarkServerChildProcess::Launch(const std::string_view runId,
                                                                         const std::string_view configPath)
    {
        return LaunchProcess(LaunchTarget::BenchmarkServerChild, {}, runId, configPath, {});
    }

    BenchmarkServerChildLaunchResult BenchmarkServerChildProcess::LaunchWorldHost(const std::string_view executablePath,
                                                                                  const std::string_view runId,
                                                                                  const std::string_view configPath,
                                                                                  const std::string_view runsRoot)
    {
        return LaunchProcess(LaunchTarget::WorldServerHost, executablePath, runId, configPath, runsRoot);
    }

    BenchmarkServerChildLaunchResult BenchmarkServerChildProcess::LaunchProcess(const LaunchTarget target,
                                                                                const std::string_view executablePath,
                                                                                const std::string_view runId,
                                                                                const std::string_view configPath,
                                                                                const std::string_view runsRoot)
    {
        // for controller process
        BenchmarkIpcOwnedHandle commandWriteHandle;
        BenchmarkIpcOwnedHandle eventReadHandle;

        // for server child process
        BenchmarkIpcOwnedHandle commandReadHandle;
        BenchmarkIpcOwnedHandle eventWriteHandle;

        Win32OwnedHandle processHandle;

        try
        {
            if (runId.empty() || runId.size() > MaximumRunIdBytes)
            {
                return LaunchFailure("runId must contain between 1 and 128 bytes");
            }
            if (target == LaunchTarget::WorldServerHost &&
                (executablePath.empty() || configPath.empty() || runsRoot.empty() ||
                 !std::filesystem::path{executablePath}.is_absolute() ||
                 !std::filesystem::path{configPath}.is_absolute() || !std::filesystem::path{runsRoot}.is_absolute()))
            {
                return LaunchFailure("World Host executable, config, and runs root paths must be absolute");
            }

            SECURITY_ATTRIBUTES securityAttributes{};
            securityAttributes.nLength = sizeof(securityAttributes);
            securityAttributes.bInheritHandle = TRUE; // handle 상속 허용

            // command handle 생성(controller process -> server child process)
            HANDLE commandRead = nullptr;
            HANDLE commandWrite = nullptr;
            if (CreatePipe(&commandRead, &commandWrite, &securityAttributes, 0) == FALSE)
            {
                return LaunchFailure("failed to create command pipe", GetLastError());
            }
            commandReadHandle.Reset(commandRead);
            commandWriteHandle.Reset(commandWrite);

            // event handle 생성(server child process -> controller process)
            HANDLE eventRead = nullptr;
            HANDLE eventWrite = nullptr;
            if (CreatePipe(&eventRead, &eventWrite, &securityAttributes, 0) == FALSE)
            {
                return LaunchFailure("failed to create event pipe", GetLastError());
            }
            eventReadHandle.Reset(eventRead);
            eventWriteHandle.Reset(eventWrite);

            std::uint32_t nativeErrorCode = 0;
            if (!MakeNonInheritable(static_cast<HANDLE>(commandWriteHandle.Get()), &nativeErrorCode) ||
                !MakeNonInheritable(static_cast<HANDLE>(eventReadHandle.Get()), &nativeErrorCode))
            {
                return LaunchFailure("failed to make Controller pipe endpoint non-inheritable", nativeErrorCode);
            }

            // child process에 전달할 handle은 상속 가능하게 설정됨
            std::array<HANDLE, 2> inheritedHandles = {
                static_cast<HANDLE>(commandReadHandle.Get()),
                static_cast<HANDLE>(eventWriteHandle.Get()),
            };

            ProcessThreadAttributeList attributeList; // for handle whitelist
            if (!attributeList.Initialize(&nativeErrorCode))
            {
                return LaunchFailure("failed to initialize child process attribute list", nativeErrorCode);
            }

            if (!attributeList.SetInheritedHandles(inheritedHandles.data(), inheritedHandles.size(), &nativeErrorCode))
            {
                return LaunchFailure("failed to restrict inherited child handles", nativeErrorCode);
            }

            std::wstring wideExecutablePath;
            if (target == LaunchTarget::BenchmarkServerChild)
            {
                if (!CurrentExecutablePath(&wideExecutablePath, &nativeErrorCode))
                {
                    return LaunchFailure("failed to resolve Benchmark executable path", nativeErrorCode);
                }
            }
            else if (!Utf8ToWide(executablePath, &wideExecutablePath, &nativeErrorCode))
            {
                return LaunchFailure("World Host executable path is not valid UTF-8", nativeErrorCode);
            }

            std::wstring wideRunId;
            if (!Utf8ToWide(runId, &wideRunId, &nativeErrorCode))
            {
                return LaunchFailure("runId is not valid UTF-8", nativeErrorCode);
            }

            std::wstring wideConfigPath;
            if (!Utf8ToWide(configPath, &wideConfigPath, &nativeErrorCode))
            {
                return LaunchFailure("config path is not valid UTF-8", nativeErrorCode);
            }

            std::wstring wideRunsRoot;
            if (!Utf8ToWide(runsRoot, &wideRunsRoot, &nativeErrorCode))
            {
                return LaunchFailure("runs root path is not valid UTF-8", nativeErrorCode);
            }

            std::wstring commandLine;
            if (target == LaunchTarget::BenchmarkServerChild)
            {
                commandLine = BuildBenchmarkChildCommandLine(
                    wideExecutablePath, static_cast<HANDLE>(commandReadHandle.Get()),
                    static_cast<HANDLE>(eventWriteHandle.Get()), wideRunId, wideConfigPath);
            }
            else
            {
                commandLine = BuildWorldHostCommandLine(
                    wideExecutablePath, static_cast<HANDLE>(commandReadHandle.Get()),
                    static_cast<HANDLE>(eventWriteHandle.Get()), wideRunId, wideConfigPath, wideRunsRoot);
            }
            std::vector<wchar_t> mutableCommandLine(commandLine.cbegin(), commandLine.cend());
            mutableCommandLine.push_back(L'\0'); // null char

            STARTUPINFOEXW startupInfo{};
            startupInfo.StartupInfo.cb = sizeof(startupInfo);
            startupInfo.lpAttributeList = attributeList.Get();

            PROCESS_INFORMATION processInformation{};

            /*
              CreateProcessW(
              executablePath.c_str(),       // 실행 파일 경로
              mutableCommandLine.data(),    // 커맨드 라인 명령어(mutable)
              nullptr,
              nullptr,
              TRUE,                         // 상속 허용
              creationFlags,                // 생성 옵션 플래그
              nullptr,
              nullptr,
              &startupInfo.StartupInfo,
              &processInformation);
            */

            /*
            EXTENDED_STARTUPINFO_PRESENT
                - STARTUPINFOEX 와 handle whitelist 사용
            CREATE_NEW_PROCESS_GROUP
                - Controller 와 다른 별도의 프로세스 그룹 생성
                - Server Child 를 console process group의 root로 사용하기 위한 프로세스 그룹 생성
                    - Controller Process Group / Server Child Process Group
            */

            const DWORD creationFlags = EXTENDED_STARTUPINFO_PRESENT | CREATE_NEW_PROCESS_GROUP;
            const BOOL createSucceeded =
                CreateProcessW(wideExecutablePath.c_str(), mutableCommandLine.data(), nullptr, nullptr, TRUE,
                               creationFlags, nullptr, nullptr, &startupInfo.StartupInfo, &processInformation);
            if (createSucceeded == FALSE)
            {
                return LaunchFailure("failed to create Server child process", GetLastError());
            }

            processHandle.Reset(processInformation.hProcess); // 생성된 프로세스 handle
            CloseHandle(processInformation.hThread);          // primary thread는 계속 실행되고 HANDLE 참조만 닫음
            commandReadHandle.Reset();
            eventWriteHandle.Reset();

            BenchmarkServerChildLaunchResult result;
            result.child = std::unique_ptr<BenchmarkServerChildProcess>(new BenchmarkServerChildProcess(
                processHandle.Release(), std::move(commandWriteHandle), std::move(eventReadHandle)));
            return result;
        }
        catch (const std::bad_alloc&)
        {
            if (processHandle.Get() != nullptr)
            {
                TerminateAndWait(processHandle.Get());
            }
            return LaunchFailure("Server child process launch allocation failed");
        }
        catch (...)
        {
            if (processHandle.Get() != nullptr)
            {
                TerminateAndWait(processHandle.Get());
            }
            return LaunchFailure("Server child process launch failed with an unknown error");
        }
    }

    BenchmarkNativeProcessHandle BenchmarkServerChildProcess::ProcessHandle() const noexcept
    {
        return processHandle_;
    }

    BenchmarkIpcLineWriter& BenchmarkServerChildProcess::CommandWriter() noexcept
    {
        return commandWriter_;
    }

    BenchmarkIpcLineReader& BenchmarkServerChildProcess::EventReader() noexcept
    {
        return eventReader_;
    }

    BenchmarkServerChildExitResult BenchmarkServerChildProcess::WaitForExit(
        const std::uint32_t timeoutMilliseconds) const
    {
        const HANDLE processHandle = static_cast<HANDLE>(processHandle_);
        if (processHandle == nullptr || processHandle == INVALID_HANDLE_VALUE)
        {
            return BenchmarkServerChildExitResult{"Server child process handle is invalid"};
        }

        // 프로세스가 종료될 때 Windows Kernel에서 kernel object를 signaled로 변경(= processHandle이 가리킴)
        const DWORD waitResult = WaitForSingleObject(processHandle, timeoutMilliseconds);
        if (waitResult == WAIT_TIMEOUT)
        {
            return BenchmarkServerChildExitResult{"Server child did not exit before the shutdown timeout"};
        }
        if (waitResult != WAIT_OBJECT_0)
        {
            return BenchmarkServerChildExitResult{"failed to wait for Server child process", GetLastError()};
        }

        DWORD exitCode = 0;
        if (GetExitCodeProcess(processHandle, &exitCode) == FALSE) // 종료 결과 조회
        {
            return BenchmarkServerChildExitResult{"failed to read Server child process exit code", GetLastError()};
        }

        BenchmarkServerChildExitResult result;
        result.exitCode = exitCode;
        return result;
    }
} // namespace psnr::benchmark
