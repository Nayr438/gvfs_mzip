#pragma once

#include <cstdint>
#include <ctime>
#include <string>

#include "MZip.h"

class MZipUtil : public MZip
{
  static zip::LocalFileHeader getLocalFileHeader(const zip::CentralDirectoryFileHeader &CentralDirectory);
  static zip::CentralDirectoryFileHeader createCentralDirectoryFileHeader(DOSDateTime DosDateTime, std::uint32_t crc,
                                                                          std::uint32_t CompressedFileSize,
                                                                          std::uint32_t UnCompressedFileSize,
                                                                          std::uint16_t FileNameLength,
                                                                          std::uint32_t FileHeaderOffset);
  static zip::EndOfCentralDirectoryRecord
  createCentralDirectoryEnd(std::uint16_t FileCount, std::uint32_t DirectorySize, std::uint32_t DirectoryOffset);

public:
  static void extractArchive(const char *Path);
  static void createArchive(const std::string& Path, int Version);
};