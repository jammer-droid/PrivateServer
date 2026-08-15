#include "BenchmarkProtocol.h"

namespace psnr::benchmark
{
    namespace
    {
        // Golden vector는 wire layout 검증용 고정값이며 실제 요청의 timestamp로 사용하지 않는다.
        // 모든 정수는 little-endian이다.
        //
        // Offset  Size  Field
        // 0       2     protocolVersion
        // 2       2     operation
        // 4       4     clientId
        // 8       8     sequence
        // 16      8     clientSendTimestampNanoseconds
        // 24      8     serverReceivedTimestampNanoseconds
        // 32      8     serverResponsePreparedTimestampNanoseconds
        // 40      24    deterministic padding (0x00..0x17)
        constexpr BenchmarkPayload GoldenPayloadFields{
            BenchmarkProtocolVersion,
            BenchmarkOperation::Echo,
            0x01020304,
            0x0102030405060708,
            0x1112131415161718,
            0x2122232425262728,
            0x3132333435363738,
        };

        constexpr BenchmarkProtocolCodec::CanonicalPayload GoldenPayload = {
            std::byte{0x02}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03},
            std::byte{0x02}, std::byte{0x01}, std::byte{0x08}, std::byte{0x07}, std::byte{0x06}, std::byte{0x05},
            std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01}, std::byte{0x18}, std::byte{0x17},
            std::byte{0x16}, std::byte{0x15}, std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11},
            std::byte{0x28}, std::byte{0x27}, std::byte{0x26}, std::byte{0x25}, std::byte{0x24}, std::byte{0x23},
            std::byte{0x22}, std::byte{0x21}, std::byte{0x38}, std::byte{0x37}, std::byte{0x36}, std::byte{0x35},
            std::byte{0x34}, std::byte{0x33}, std::byte{0x32}, std::byte{0x31}, std::byte{0x00}, std::byte{0x01},
            std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05}, std::byte{0x06}, std::byte{0x07},
            std::byte{0x08}, std::byte{0x09}, std::byte{0x0A}, std::byte{0x0B}, std::byte{0x0C}, std::byte{0x0D},
            std::byte{0x0E}, std::byte{0x0F}, std::byte{0x10}, std::byte{0x11}, std::byte{0x12}, std::byte{0x13},
            std::byte{0x14}, std::byte{0x15}, std::byte{0x16}, std::byte{0x17},
        };

        static_assert(sizeof(BenchmarkOperation) == sizeof(std::uint16_t));
        static_assert(BenchmarkRequestPacketType == 0x7F01);
        static_assert(BenchmarkResponsePacketType == 0x7F02);
        static_assert(BenchmarkProtocolCodec::EncodeCanonical(GoldenPayloadFields) == GoldenPayload);
        static_assert(BenchmarkProtocolCodec::DecodeCanonical(GoldenPayload) == GoldenPayloadFields);
        static_assert(BenchmarkProtocolCodec::HasDeterministicPadding(GoldenPayload));
    } // namespace
} // namespace psnr::benchmark
