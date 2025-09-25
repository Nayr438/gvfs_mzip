#include "DosDateTime.h"
#include <cstdint>
#pragma once
#pragma pack(push, 2)

namespace zip
{
static constexpr std::uint32_t Signature = 0x04034b50;

struct LocalFileHeader
{
  std::uint32_t Signature;
  std::uint16_t Version;
  std::uint16_t Flags;
  std::uint16_t Compression;
  DOSDateTime LastModified;
  std::uint32_t CRC32;
  std::uint32_t CompressedSize;
  std::uint32_t UncompressedSize;
  std::uint16_t FileNameLength;
  std::uint16_t ExtraFieldLength;

  // FileName
  // ExtraField
};

struct CentralDirectoryFileHeader
{
  std::uint32_t Signature;
  std::uint16_t Version;
  std::uint16_t MinVersion;
  std::uint16_t BitFlag;
  std::uint16_t CompressionMethod;
  DOSDateTime LastModified;
  std::uint32_t CRC32;
  std::uint32_t CompressedSize;
  std::uint32_t UncompressedSize;
  std::uint16_t FileNameLength;
  std::uint16_t ExtraFieldLength;
  std::uint16_t CommentLength;
  std::uint16_t DiskStartNum;
  std::uint16_t InternalFileAttributes;
  std::uint32_t ExternalFileAttributes;
  std::uint32_t FileHeaderOffset;

  // FileName
  // ExtraField
  // FileComment
};

struct EndOfCentralDirectoryRecord
{
  std::uint32_t Signature;
  std::uint16_t DiskNumber;
  std::uint16_t DiskStartNumber;
  std::uint16_t DirectoryCountOnDisk;
  std::uint16_t DirectoryCountTotal;
  std::uint32_t CentralDirectorySize;
  std::uint32_t CentralDirectoryOffset;
  std::uint16_t CommentLength;
  // Comment
};

} // namespace zip

#pragma pack(pop)