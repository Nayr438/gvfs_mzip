#pragma once
#include <cstdint>

namespace mzip {
    static constexpr std::uint32_t LocalFileHeaderSignature = 0x04034b50;
    static constexpr std::uint32_t CentralDirectorySignature = 0x02014b50;
    static constexpr std::uint32_t CentralDirectoryEndSignature = 0x06054b50;
    static constexpr std::uint32_t CentralDirectoryEndSignatureLegacy = 0x05030208;

    enum class Version
    {
        Mrs1 = 1,
        Mrs2 = 2,
        ForcedRecovery = 25
    };
}
