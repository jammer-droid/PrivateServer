#include "BenchmarkIpcPipe.h"

#define NOMINMAX
#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace psnr::benchmark
{
    namespace
    {
        constexpr std::size_t ReadBufferBytes = 4096;

        [[nodiscard]] bool IsValidHandle(const BenchmarkIpcPipeHandle handle) noexcept
        {
            return handle != nullptr && handle != INVALID_HANDLE_VALUE;
        }

        [[nodiscard]] BenchmarkIpcIoResult IoFailure(std::string error, const std::uint32_t nativeErrorCode = 0)
        {
            BenchmarkIpcIoResult result;
            result.error = std::move(error);
            result.nativeErrorCode = nativeErrorCode;
            return result;
        }

        [[nodiscard]] BenchmarkIpcReadResult ReadFailure(std::string error, const std::uint32_t nativeErrorCode = 0)
        {
            BenchmarkIpcReadResult result;
            result.error = std::move(error);
            result.nativeErrorCode = nativeErrorCode;
            return result;
        }

        [[nodiscard]] BenchmarkIpcIoResult WriteExact(const HANDLE handle, const std::string_view bytes)
        {
            DWORD writtenBytes = 0;
            const BOOL writeSucceeded =
                WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &writtenBytes, nullptr);
            if (writeSucceeded == FALSE)
            {
                return IoFailure("failed to write IPC pipe", GetLastError());
            }
            if (static_cast<std::size_t>(writtenBytes) != bytes.size())
            {
                return IoFailure("IPC pipe write did not write the complete message");
            }
            return {};
        }
    } // namespace

    BenchmarkIpcOwnedHandle::BenchmarkIpcOwnedHandle(const BenchmarkIpcPipeHandle handle) noexcept
        : handle_(handle)
    {
    }

    BenchmarkIpcOwnedHandle::~BenchmarkIpcOwnedHandle() noexcept
    {
        Reset();
    }

    BenchmarkIpcOwnedHandle::BenchmarkIpcOwnedHandle(BenchmarkIpcOwnedHandle&& other) noexcept
        : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    BenchmarkIpcOwnedHandle& BenchmarkIpcOwnedHandle::operator=(BenchmarkIpcOwnedHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    BenchmarkIpcPipeHandle BenchmarkIpcOwnedHandle::Get() const noexcept
    {
        return handle_;
    }

    bool BenchmarkIpcOwnedHandle::IsValid() const noexcept
    {
        return IsValidHandle(handle_);
    }

    void BenchmarkIpcOwnedHandle::Reset(const BenchmarkIpcPipeHandle handle) noexcept
    {
        if (IsValid())
        {
            CloseHandle(static_cast<HANDLE>(handle_));
        }
        handle_ = handle;
    }

    BenchmarkIpcLineReader::BenchmarkIpcLineReader(BenchmarkIpcOwnedHandle&& handle) noexcept
        : handle_(std::move(handle))
    {
    }

    BenchmarkIpcReadResult BenchmarkIpcLineReader::ReadLine(std::string* const outLine)
    {
        if (outLine == nullptr)
        {
            return ReadFailure("outLine must not be null");
        }
        if (!handle_.IsValid())
        {
            return ReadFailure("IPC pipe read handle is invalid");
        }

        while (true)
        {
            const std::size_t newlineIndex = pendingBytes_.find('\n');
            if (newlineIndex != std::string::npos)
            {
                if (newlineIndex > BenchmarkIpcMaximumLineBytes)
                {
                    return ReadFailure("IPC pipe line exceeds the maximum size");
                }

                std::string line(pendingBytes_.data(), newlineIndex);
                pendingBytes_.erase(0, newlineIndex + 1);
                *outLine = std::move(line);

                BenchmarkIpcReadResult result;
                result.outcome = BenchmarkIpcReadOutcome::Line;
                return result;
            }
            if (pendingBytes_.size() > BenchmarkIpcMaximumLineBytes)
            {
                return ReadFailure("IPC pipe line exceeds the maximum size");
            }

            std::array<char, ReadBufferBytes> buffer{};
            DWORD readBytes = 0;
            const BOOL readSucceeded = ReadFile(static_cast<HANDLE>(handle_.Get()), buffer.data(),
                                                static_cast<DWORD>(buffer.size()), &readBytes, nullptr);
            if (readSucceeded == FALSE)
            {
                const DWORD nativeErrorCode = GetLastError();
                if (nativeErrorCode == ERROR_BROKEN_PIPE)
                {
                    if (pendingBytes_.empty())
                    {
                        return {};
                    }
                    return ReadFailure("IPC pipe ended with an incomplete line", nativeErrorCode);
                }
                return ReadFailure("failed to read IPC pipe", nativeErrorCode);
            }
            if (readBytes == 0)
            {
                if (pendingBytes_.empty())
                {
                    return {};
                }
                return ReadFailure("IPC pipe ended with an incomplete line");
            }

            pendingBytes_.append(buffer.data(), readBytes);
        }
    }

    BenchmarkIpcLineWriter::BenchmarkIpcLineWriter(BenchmarkIpcOwnedHandle&& handle) noexcept
        : handle_(std::move(handle))
    {
    }

    BenchmarkIpcIoResult BenchmarkIpcLineWriter::WriteLine(const std::string_view line)
    {
        if (!handle_.IsValid())
        {
            return IoFailure("IPC pipe write handle is invalid");
        }
        if (line.size() > BenchmarkIpcMaximumLineBytes)
        {
            return IoFailure("IPC pipe line exceeds the maximum size");
        }
        if (line.find('\n') != std::string_view::npos)
        {
            return IoFailure("IPC pipe line must not contain a newline");
        }

        const HANDLE nativeHandle = static_cast<HANDLE>(handle_.Get());
        const BenchmarkIpcIoResult lineResult = WriteExact(nativeHandle, line);
        if (!lineResult.Succeeded())
        {
            return lineResult;
        }
        return WriteExact(nativeHandle, "\n");
    }
} // namespace psnr::benchmark
