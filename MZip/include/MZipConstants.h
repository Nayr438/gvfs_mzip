#pragma once
#include <cstdint>
#include "ZipStructs.h"

namespace mzip
{
namespace v1
{
static constexpr std::uint32_t LocalFileHeaderSignature = zip::LocalFileHeaderSignature;
static constexpr std::uint32_t CentralDirectoryEndSignature = 0xdd59fc12;
static constexpr std::uint32_t CentralDirectoryEndSignature2 = 0x05030207;
static constexpr std::uint32_t CentralDirectorySignature = 0x05024b80;
static constexpr std::uint32_t Signature = 0x85840000;
static constexpr std::uint32_t Signature2 = 0x6c4ed59e;
} // namespace v1
namespace v2
{
static constexpr std::uint32_t LocalFileHeaderSignature = zip::LocalFileHeaderSignature;
static constexpr std::uint32_t CentralDirectorySignature = zip::CentralDirectoryFileHeaderSignature;
static constexpr std::uint32_t CentralDirectoryEndSignature = 0x05030208;
static constexpr std::uint32_t CentralDirectoryEndSignature2 = 0x06054b50;
static constexpr std::uint32_t Signature = 0xdfe7a57d;
} // namespace v2
namespace v3
{
static constexpr std::uint32_t RecoverySeed = 0x7693d7fb;
static constexpr std::uint32_t LocalFileHeaderSignature = 0x02014b50;
static constexpr std::uint32_t LocalFileHeaderSignature2 = 0x4034b50;
static constexpr std::uint32_t CentralDirectorySignature = zip::CentralDirectoryFileHeaderSignature;
static constexpr std::uint32_t CentralDirectoryEndSignature = v2::CentralDirectoryEndSignature;
static constexpr std::uint32_t Signature = 0x10355137;
} // namespace v3
namespace MG2
{
static constexpr std::uint32_t LocalFileHeaderSignature = zip::LocalFileHeaderSignature;
static constexpr std::uint32_t CentralDirectorySignature = zip::CentralDirectoryFileHeaderSignature;
static constexpr std::uint32_t CentralDirectorySignature2 = 0x8428cef0;
static constexpr std::uint32_t CentralDirectoryEndSignature = v2::CentralDirectoryEndSignature;
static constexpr std::uint32_t Signature = 0x0729e45f;

} // namespace MG2

enum class Version
{
  Mrs1 = 1,
  Mrs2 = 2,
  Mrs3 = 3,
  MG2 = 4,
  ForcedRecovery
};
} // namespace mzip