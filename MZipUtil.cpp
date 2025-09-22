#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

#include "MZip.h"
#include "MZipUtil.h"
#include "ZipStructs.h"
#include "zlib.h"

zip::CentralDirectoryFileHeader
MZipUtil::createCentralDirectoryFileHeader(DOSDateTime DosDateTime, std::uint32_t crc,
                                         std::uint32_t CompressedFileSize, std::uint32_t UnCompressedFileSize,
                                         std::uint16_t FileNameLength, std::uint32_t FileHeaderOffset)
{
    zip::CentralDirectoryFileHeader header;
    header.Signature = MZip::CentralDirectorySignature;
    header.Version = 25;
    header.MinVersion = 20;
    header.BitFlag = 0;
    header.CompressionMethod = 8;
    header.LastModified = DosDateTime;
    header.CRC32 = crc;
    header.CompressedSize = CompressedFileSize;
    header.UncompressedSize = UnCompressedFileSize;
    header.FileNameLength = FileNameLength;
    header.ExtraFieldLength = 0;
    header.CommentLength = 0;
    header.DiskStartNum = 0;
    header.InternalFileAttributes = 0;
    header.ExternalFileAttributes = 0;
    header.FileHeaderOffset = FileHeaderOffset;
    return header;
}

zip::EndOfCentralDirectoryRecord
MZipUtil::createCentralDirectoryEnd(std::uint16_t FileCount, std::uint32_t DirectorySize, std::uint32_t DirectoryOffset)
{
  // Aggregate initialization with reuse of FileCount
  return {
      MZip::CentralDirectoryEndSignature, // Signature
      0,                                  // Number of this disk
      0,                                  // Disk where central directory starts
      FileCount,                          // Number of central directory records on this disk
      FileCount,                          // Total number of central directory records
      DirectorySize,                      // Size of the central directory (bytes)
      DirectoryOffset,                    // Offset of start of central directory
      0                                   // Comment length (no comment)
  };
}

zip::LocalFileHeader MZipUtil::getLocalFileHeader(const zip::CentralDirectoryFileHeader &CentralDirectory)
{
  // Aggregate initialization to construct LocalFileHeader
  return {
      MZip::LocalFileHeaderSignature,     // Signature
      CentralDirectory.Version,           // Version needed to extract
      CentralDirectory.BitFlag,           // General purpose bit flag
      CentralDirectory.CompressionMethod, // Compression method
      CentralDirectory.LastModified,      // Last modification time/date
      CentralDirectory.CRC32,             // CRC-32
      CentralDirectory.CompressedSize,    // Compressed size
      CentralDirectory.UncompressedSize,  // Uncompressed size
      CentralDirectory.FileNameLength,    // File name length
      CentralDirectory.ExtraFieldLength   // Extra field length
  };
}

void MZipUtil::extractArchive(const char *Path)
{
  // Create a path object from the input path, replace the file extension (if any), and make it platform-preferred
  const auto p = std::filesystem::path(Path).replace_extension().make_preferred();

  // Create the root directory for extracting files
  std::filesystem::create_directories(p);

  // Initialize MZip object with the path to the archive
  MZip MRSArchive(Path);

  // Iterate over each file entry in the archive
  for (const auto &Entry : MRSArchive.FileList)
  {
    // Find the last '/' in the file path to separate directories from the file name
    auto q = Entry.first.find_last_of("/");

    if (q != std::string::npos)
    {
      // Extract the directory path from the file path
      auto dir = std::filesystem::path(p).append(Entry.first.substr(0, q)).make_preferred();
      // Create the directory structure if it does not exist
      std::filesystem::create_directories(dir);
    }

    // Retrieve the uncompressed data for the current file from the archive
    auto data = GetFile(p, Entry.second);

    // Construct the full path for the output file
    auto FileOutPath = std::filesystem::path(p).append(Entry.first).make_preferred();

    // Open the output file for writing
    std::ofstream File(FileOutPath, std::ios::binary);
    if (!File)
    {
      throw std::runtime_error("Failed to open file for writing: " + FileOutPath.string());
    }

    // Write the uncompressed data to the output file
    File.write(data.get(), Entry.second.UnCompressedSize);

    // Set the last write time of the file to match the original timestamp from the archive
    //std::filesystem::last_write_time(FileOutPath, Entry.second.LastModified);
  }
}

void MZipUtil::createArchive(const std::string &Path, int Version)
{
  // Define the path for the archive file with .mrs extension
  const std::string archivePath = Path + ".mrs";

  // Open the archive file for writing in binary mode
  std::ofstream MRSArchive(archivePath, std::ios::binary);
  if (!MRSArchive)
  {
    throw std::runtime_error("Failed to open archive file.");
  }

  // Map to store the central directory file headers for each file
  std::map<std::string, zip::CentralDirectoryFileHeader> FileEntries;

  // Iterate through each file in the directory and its subdirectories
  for (const auto &entry : std::filesystem::recursive_directory_iterator(Path))
  {
    if (std::filesystem::is_regular_file(entry))
    {
      const auto filePath = entry.path();

      // Open the file for reading in binary mode, positioning at the end to get file size
      std::ifstream file(filePath, std::ios::binary | std::ios::ate);
      if (!file)
      {
        throw std::runtime_error("Failed to open file: " + filePath.string());
      }

      // Get the size of the uncompressed file
      const auto uncompressedSize = file.tellg();
      file.seekg(0, std::ios::beg);

      // Allocate memory for uncompressed and compressed data
      auto uncompressedData = std::make_unique<char[]>(uncompressedSize);
      auto compressedData = std::make_unique<char[]>(uncompressedSize);

      // Read the entire file into memory
      file.read(uncompressedData.get(), uncompressedSize);

      // Compute CRC32 checksum of the uncompressed data
      uLong crc = crc32(0L, nullptr, 0);
      crc = crc32(crc, reinterpret_cast<Bytef *>(uncompressedData.get()), uncompressedSize);

      // Compress the uncompressed data
      const auto compressedSize = MZip::compress(uncompressedData.get(), compressedData.get(), uncompressedSize);

      if (compressedSize <= 0)
      {
        throw std::runtime_error("Compression failed for file: " + filePath.string());
      }

      // Extract the relative file name and its size
      const auto fileName = filePath.string().substr(Path.length() + 1);
      const auto fileNameSize = fileName.length();
      auto fileNameData = std::make_unique<char[]>(fileNameSize + 1);
      std::copy(fileName.begin(), fileName.end(), fileNameData.get());
      fileNameData[fileNameSize] = '\0'; // Ensure null termination

      // Apply version-specific character conversion if needed
      if (Version == 2)
      {
        ConvertChar(fileNameData.get(), fileNameSize);
      }

      // Create a central directory file header for this file
      auto centralDirectory = createCentralDirectoryFileHeader(DOSDateTime(entry.last_write_time()), crc,
                                                               compressedSize, uncompressedSize, fileNameSize,
                                                               static_cast<std::uint32_t>(MRSArchive.tellp()));

      // Get the local file header corresponding to the central directory header
      auto fileHeader = getLocalFileHeader(centralDirectory);

      // Apply version-specific character conversion to the file header if needed
      if (Version == 2)
      {
        ConvertChar(reinterpret_cast<char *>(&fileHeader), sizeof(fileHeader));
      }

      // Write the local file header to the archive
      MRSArchive.write(reinterpret_cast<char *>(&fileHeader), sizeof(fileHeader));
      // Write the file name to the archive
      MRSArchive.write(fileNameData.get(), fileNameSize);
      // Write the compressed file data to the archive
      MRSArchive.write(compressedData.get(), compressedSize);

      // Store the central directory header in the map
      FileEntries[fileNameData.get()] = centralDirectory;
    }
  }

  // Record the offset of the central directory in the archive
  const auto directoryOffset = static_cast<uint32_t>(MRSArchive.tellp());

  // Write the central directory headers for all files
  for (const auto &entry : FileEntries)
  {
    auto centralDirectory = entry.second;

    auto fileNameLength = centralDirectory.FileNameLength;

    // Apply version-specific character conversion to the central directory header if needed
    if (Version == 2)
    {
      ConvertChar(reinterpret_cast<char *>(&centralDirectory), sizeof(centralDirectory));
    }

    // Write the central directory header to the archive
    MRSArchive.write(reinterpret_cast<char *>(&centralDirectory), sizeof(centralDirectory));
    // Write the file name to the archive
    MRSArchive.write(entry.first.c_str(), fileNameLength);
  }

  // Compute the size of the central directory
  const auto directorySize = static_cast<std::uint16_t>(MRSArchive.tellp()) - directoryOffset;

  // Create and write the end of central directory record
  auto centralDirectoryEnd =
      createCentralDirectoryEnd(static_cast<std::uint16_t>(FileEntries.size()), directorySize, directoryOffset);

  // Apply version-specific character conversion to the end of central directory record if needed
  if (Version == 2)
  {
    ConvertChar(reinterpret_cast<char *>(&centralDirectoryEnd), sizeof(centralDirectoryEnd));
  }

  // Write the end of central directory record to the archive
  MRSArchive.write(reinterpret_cast<char *>(&centralDirectoryEnd), sizeof(centralDirectoryEnd));
}
