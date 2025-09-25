#include "MZipRecovery.h"
#include "DosDateTime.h"
#include "MZip.h"
#include "MZipConstants.h"
#include "ZipStructs.h"
#include <cstring>
#include <iostream>
#include <zlib.h>

bool MZipRecovery::openArchiveForced()
{
  std::cout << "Attempting to scan for data using pattern matching.\n"
            << "Files will not have names, some files may be missing, and some data may be invalid.\n"
            << "This does not guarantee any valid data will be found.\n";

  archiveFile = std::make_unique<std::fstream>(archivePath, std::ios::in | std::ios::binary);
  if (!archiveFile->is_open())
    return false;

  // Get first LocalFileHeader signature then scan for other instances
  std::uint32_t signature = 0;
  archiveFile->seekg(0);
  archiveFile->read(reinterpret_cast<char*>(&signature), sizeof(signature));

  const auto fileSize = std::filesystem::file_size(archivePath) - sizeof(zip::EndOfCentralDirectoryRecord);
  if (fileSize <= 0)
    return false;

  _version = mzip::Version::ForcedRecovery;
  ArchiveTree = std::make_shared<ZipTree>();
  archiveFile->seekg(0);

  // Scan file for matching signatures
  std::vector<std::streampos> signaturePositions;
  std::vector<char> buffer(4096);

  while (archiveFile->good() && archiveFile->tellg() < static_cast<std::streamoff>(fileSize))
  {
    archiveFile->read(buffer.data(), buffer.size());
    const auto bytesRead =
        static_cast<size_t>(archiveFile->tellg() - (archiveFile->tellg() - static_cast<std::streampos>(buffer.size())));
    if (bytesRead < 4)
      break;

    // Check each position in buffer for signature
    for (size_t i = 0; i <= bytesRead - 4; ++i)
    {
      std::uint32_t currentSignature;
      std::memcpy(&currentSignature, buffer.data() + i, sizeof(uint32_t));

      if (currentSignature == signature)
      {
        signaturePositions.push_back(archiveFile->tellg() - static_cast<std::streampos>(bytesRead) +
                                     static_cast<std::streampos>(i));
      }
    }

    // Back up to catch split signatures
    if (bytesRead >= 3)
      archiveFile->seekg(-3, std::ios::cur);
  }

  std::cout << "Found " << signaturePositions.size() << " potential file signatures\n";

  // Process found signatures
  for (size_t pos = 0; pos < signaturePositions.size(); pos++)
  {
    const auto currentPos = signaturePositions[pos];
    const auto nextPos =
        (pos < signaturePositions.size() - 1) ? signaturePositions[pos + 1] : static_cast<std::streampos>(fileSize);
    const auto size = static_cast<size_t>(nextPos - currentPos);

    std::cout << "Processing signature at position " << currentPos << " (file " << pos + 1 << "/" << signaturePositions.size() << ")\n";

    // Skip LocalFileHeader when reading data
    archiveFile->clear();
    archiveFile->seekg(currentPos + static_cast<std::streamoff>(sizeof(zip::LocalFileHeader)), std::ios::beg);
    if (!archiveFile->good())
      continue;

    auto compressedData = std::make_unique<char[]>(size);
    archiveFile->read(compressedData.get(), size);

    zip::CentralDirectoryFileHeader dirHeader{};
    dirHeader.Signature = mzip::v2::CentralDirectorySignature;
    dirHeader.CompressionMethod = 8;
    dirHeader.LastModified = DOSDateTime(std::filesystem::last_write_time(archivePath));
    dirHeader.CompressedSize = static_cast<uint32_t>(size);
    dirHeader.UncompressedSize = static_cast<uint32_t>(size * 2);
    dirHeader.FileHeaderOffset =
        static_cast<uint32_t>(currentPos + static_cast<std::streamoff>(sizeof(zip::LocalFileHeader)));

    std::string fileName = "file_" + std::to_string(pos);
    if (findData(std::span{compressedData.get(), size}, dirHeader, fileName))
    {
      ArchiveTree->insert(fileName, dirHeader);
    }
  }

  archiveFile->clear();

  return !signaturePositions.empty();
}

bool MZipRecovery::findData(std::span<char> inData, zip::CentralDirectoryFileHeader &header, std::string &fileName)
{
  constexpr size_t maxSize = 16777216; // 16MB
  auto uncompressedData = std::make_unique<char[]>(maxSize);

  // Try each possible starting position
  for (size_t offset = 0; offset < inData.size() - 1; offset++)
  {
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef *>(inData.data() + offset);
    stream.avail_in = static_cast<uInt>(inData.size() - offset);
    stream.next_out = reinterpret_cast<Bytef *>(uncompressedData.get());
    stream.avail_out = maxSize;

    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
    {
      inflateEnd(&stream);
      continue;
    }

    int status = ::inflate(&stream, Z_FINISH);
    inflateEnd(&stream);

    if (status == Z_STREAM_END && stream.total_in <= stream.total_out)
    {
      std::uint32_t crc = crc32(0L, nullptr, 0);
      crc = crc32(crc, reinterpret_cast<Bytef *>(uncompressedData.get()), stream.total_out);

      if (crc == 0)
        continue;

      header.FileNameLength = offset;
      header.CompressedSize = stream.total_in;
      header.UncompressedSize = stream.total_out;
      header.CRC32 = crc;

      header.FileHeaderOffset += offset;

      std::uint64_t signature;
      std::memcpy(&signature, uncompressedData.get(), sizeof(signature));

      if (auto it = signatureMap.find(signature); it != signatureMap.end())
        fileName.append(it->second);

      std::cout << std::format("Offset: {} CompressedSize: {} UncompressedSize: {} CRC32: {} File Signature: 0x{:x}\n",
                               offset, stream.total_in, stream.total_out, crc, signature);

      return true;
    }
  }

  std::cout << "No valid data found in this segment.\n";
  return false;
}