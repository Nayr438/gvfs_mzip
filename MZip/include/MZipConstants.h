#pragma once
#include <cstdint>

namespace mzip
{
namespace v1
{
static constexpr std::uint32_t LocalFileHeaderSignature = 0x85840000;
static constexpr std::uint32_t LocalFileHeaderSignature2 = 0x04034b50;
static constexpr std::uint32_t CentralDirectoryEndSignature = 0xdd59fc12; // interesting note: This matches masangsofts steam signature...
static constexpr std::uint32_t CentralDirectoryEndSignature2 = 0x05030207;
static constexpr std::uint32_t CentralDirectorySignature = 0x05024b80;

} // namespace v1
namespace v2
{
static constexpr std::uint32_t LocalFileHeaderSignature = 0x04034b50;
static constexpr std::uint32_t CentralDirectorySignature = 0x02014b50;
static constexpr std::uint32_t CentralDirectoryEndSignature = 0x05030208;
static constexpr std::uint32_t CentralDirectoryEndSignature2 = 0x06054b50;

} // namespace v2

enum class Version
{
  Mrs1 = 1,
  Mrs2 = 2,
  ForcedRecovery = 25
};
} // namespace mzip
