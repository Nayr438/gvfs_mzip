#include "MZipRecovery.h"
#include "zlib.h"
#include <cstring>
#include <iostream>

bool MZipRecovery::openArchiveForced()
{
  std::cout << "Attempting to scan for data using pattern matching.\n"
            << "Files will not have names, some files may be missing, and some data may be invalid.\n"
            << "This does not guarantee any valid data will be found.\n";

  std::ifstream file(archivePath, std::ios::binary);
  if (!file)
    return false;

  // Get initial signature and file size
  std::uint32_t signature = 0;
  file.read(reinterpret_cast<char *>(&signature), sizeof signature);

  const auto fileSize = std::filesystem::file_size(archivePath) - sizeof(zip::EndOfCentralDirectoryRecord);
  if (fileSize == 0)
    return false;

  version = mzip::Version::ForcedRecovery;
  file.seekg(0, std::ios::beg);

  // Scan file for matching signatures
  std::vector<std::streampos> signaturePositions;
  std::vector<char> buffer(4096);

  while (file && file.tellg() < static_cast<std::streamoff>(fileSize))
  {
    file.read(buffer.data(), buffer.size());
    const auto bytesRead = static_cast<size_t>(file.gcount());
    if (bytesRead < 4)
      break;

    // Check each position in buffer for signature
    for (size_t i = 0; i <= bytesRead - 4; ++i)
    {
      std::uint32_t currentSignature;
      std::memcpy(&currentSignature, buffer.data() + i, sizeof(uint32_t));

      if (currentSignature == signature)
      {
        signaturePositions.push_back(file.tellg() - static_cast<std::streampos>(bytesRead) + static_cast<std::streampos>(i));
      }
    }

    // Back up to catch split signatures
    if (bytesRead >= 3)
      file.seekg(-3, std::ios::cur);
  }

  std::cout << "Found " << signaturePositions.size() << " signatures\n";

  // Process found signatures
  for (size_t pos = 0; pos < signaturePositions.size(); pos++)
  {
    const auto currentPos = signaturePositions[pos];
    const auto nextPos =
        (pos < signaturePositions.size() - 1) ? signaturePositions[pos + 1] : static_cast<std::streampos>(fileSize);
    const auto size = static_cast<size_t>(nextPos - currentPos);

    std::cout << "Searching for data at position " << currentPos << " for file at position " << pos << "\n";

    // Skip LocalFileHeader when reading data
    file.clear();
    file.seekg(currentPos + static_cast<std::streamoff>(sizeof(zip::LocalFileHeader)), std::ios::beg);
    if (file.fail())
      continue;

    auto compressedData = std::make_unique<char[]>(size);
    file.read(compressedData.get(), size);

    zip::CentralDirectoryFileHeader dirHeader{.CompressionMethod = 8,
                                              .LastModified = DOSDateTime(std::filesystem::last_write_time(archivePath)),
                                              .CompressedSize = static_cast<uint32_t>(size),
                                              .UncompressedSize = static_cast<uint32_t>(size * 2),
                                              .FileHeaderOffset = static_cast<uint32_t>(currentPos)};

    std::string fileName = "file_" + std::to_string(pos);
    if (findData(std::span{compressedData.get(), size}, dirHeader, fileName))
    {
      ArchiveTree.insert(std::filesystem::path(fileName), dirHeader);
    }
  }

  return !signaturePositions.empty();
}

bool MZipRecovery::findData(std::span<char> inData, zip::CentralDirectoryFileHeader &header, std::string &fileName)
{
  constexpr size_t maxSize = 16777216; // 16MB
  auto uncompressedData = std::make_unique<char[]>(maxSize);

  // Try each possible starting position
  for (size_t offset = 0; offset < inData.size() - 1; offset++)
  {

    z_stream stream{.next_in = reinterpret_cast<Bytef *>(inData.data() + offset),
                    .avail_in = static_cast<uInt>(inData.size() - offset),
                    .next_out = reinterpret_cast<Bytef *>(uncompressedData.get()),
                    .avail_out = maxSize};

    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
    {
      inflateEnd(&stream);
      continue;
    }

    int status = inflate(&stream, Z_FINISH);
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

      std::uint64_t signature;
      std::memcpy(&signature, uncompressedData.get(), sizeof(signature));

      if (auto it = signatureMap.find(signature); it != signatureMap.end())
        fileName.append(it->second);

      std::cout << std::format("Offset: {} CompressedSize: {} UncompressedSize: {} CRC32: {} File Signature: 0x{:x}\n",
                               offset, stream.total_in, stream.total_out, crc, signature);
      return true;
    }
  }

  std::cout << "No data found!\n";
  return false;
}
