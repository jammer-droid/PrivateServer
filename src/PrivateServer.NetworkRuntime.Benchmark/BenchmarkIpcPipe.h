#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace psnr::benchmark
{
    using BenchmarkIpcPipeHandle = void*;

    inline constexpr std::size_t BenchmarkIpcMaximumLineBytes = 64 * 1024;

    // IPC wire format is newline-delimited JSON over a byte-stream pipe.
    // A command/event codec produces one JSON text value, WriteLine appends '\n', and ReadLine accumulates
    // partial reads until that delimiter. ReadLine removes the delimiter, preserves bytes after it for the next
    // call, and returns the complete JSON text to the corresponding codec. Newlines inside JSON strings must be
    // escaped by the JSON serializer rather than written as literal '\n' bytes.

    enum class BenchmarkIpcReadOutcome : std::uint8_t
    {
        Line,
        EndOfStream,
    };

    struct BenchmarkIpcIoResult final
    {
        std::string error;
        std::uint32_t nativeErrorCode = 0;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    struct BenchmarkIpcReadResult final
    {
        BenchmarkIpcReadOutcome outcome = BenchmarkIpcReadOutcome::EndOfStream;
        std::string error;
        std::uint32_t nativeErrorCode = 0;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    class BenchmarkIpcOwnedHandle final
    {
    public:
        BenchmarkIpcOwnedHandle() noexcept = default;
        explicit BenchmarkIpcOwnedHandle(BenchmarkIpcPipeHandle handle) noexcept;
        ~BenchmarkIpcOwnedHandle() noexcept;

        BenchmarkIpcOwnedHandle(const BenchmarkIpcOwnedHandle&) = delete;
        BenchmarkIpcOwnedHandle& operator=(const BenchmarkIpcOwnedHandle&) = delete;

        BenchmarkIpcOwnedHandle(BenchmarkIpcOwnedHandle&& other) noexcept;
        BenchmarkIpcOwnedHandle& operator=(BenchmarkIpcOwnedHandle&& other) noexcept;

        [[nodiscard]] BenchmarkIpcPipeHandle Get() const noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        void Reset(BenchmarkIpcPipeHandle handle = nullptr) noexcept;

    private:
        BenchmarkIpcPipeHandle handle_ = nullptr;
    };

    class BenchmarkIpcLineReader final
    {
    public:
        explicit BenchmarkIpcLineReader(BenchmarkIpcOwnedHandle&& handle) noexcept;

        // Synchronously reads one framed JSON line from the owned pipe endpoint.
        [[nodiscard]] BenchmarkIpcReadResult ReadLine(std::string* outLine);

    private:
        BenchmarkIpcOwnedHandle handle_;
        std::string pendingBytes_;
    };

    class BenchmarkIpcLineWriter final
    {
    public:
        explicit BenchmarkIpcLineWriter(BenchmarkIpcOwnedHandle&& handle) noexcept;

        // Synchronously writes one JSON text value followed by '\n'. Literal '\n' in line is rejected.
        [[nodiscard]] BenchmarkIpcIoResult WriteLine(std::string_view line);

    private:
        BenchmarkIpcOwnedHandle handle_;
    };
} // namespace psnr::benchmark
