#include "MZip.h"
#include "MZipConstants.h"
#include "ZipStructs.h"
#include "ZipTree.h"
#include <cstring>
#include <functional>
#include <iostream>
#include <zlib.h>

//------------------------------------------------------------------------------
// Constructor
//------------------------------------------------------------------------------

MZip::MZip(std::string_view fileName) : archivePath(fileName) {}

//------------------------------------------------------------------------------
// Core operations
//------------------------------------------------------------------------------

bool MZip::openArchive()
{
  archiveFile = std::make_unique<std::fstream>(archivePath, std::ios::in | std::ios::binary);

  if (!archiveFile->is_open())
    return false;

  std::uint32_t signature = 0;
  archiveFile->read(reinterpret_cast<char *>(&signature), sizeof(signature));

  if (signature == mzip::v1::LocalFileHeaderSignature || signature == mzip::v1::LocalFileHeaderSignature2)
    _version = mzip::Version::Mrs1;
  else
  {
    ConvertChar({reinterpret_cast<char *>(&signature), sizeof(signature)}, true);
    if (signature == mzip::v2::LocalFileHeaderSignature)
      _version = mzip::Version::Mrs2;
    else
      return false;
  }

  auto dirEnd = getEndRecord();
  archiveFile->seekg(dirEnd.CentralDirectoryOffset, std::ios::beg);

  if (!checkSignature(dirEnd))
    return false;

  return buildArchiveTree(dirEnd);
}

bool MZip::openArchiveForced() { return false; }

//------------------------------------------------------------------------------
// File operations
//------------------------------------------------------------------------------

std::shared_ptr<char[]> MZip::GetFile(std::string_view fileName)
{
  const auto *node = ArchiveTree->findFileNode(std::string(fileName));
  if (!node)
    return nullptr;

  archiveFile->seekg(node->fileHeader.FileHeaderOffset, std::ios::beg);

  if (_version != mzip::Version::ForcedRecovery)
  {
    zip::LocalFileHeader header{};
    fetchHeaderData(&header);

    if (!checkSignature(header))
      return nullptr;

    archiveFile->seekg(header.FileNameLength + header.ExtraFieldLength, std::ios::cur);
  }

  auto uncompressedData = std::make_shared<char[]>(node->fileHeader.UncompressedSize);

  if (node->fileHeader.CompressedSize == node->fileHeader.UncompressedSize)
  {
    archiveFile->read(uncompressedData.get(), node->fileHeader.UncompressedSize);
    return uncompressedData;
  }

  auto compressedData = std::make_unique<char[]>(node->fileHeader.CompressedSize);

  archiveFile->read(compressedData.get(), node->fileHeader.CompressedSize);

  auto crc = processData(std::span{compressedData.get(), node->fileHeader.CompressedSize},
                         std::span{uncompressedData.get(), node->fileHeader.UncompressedSize}, false);

  if (crc == node->fileHeader.CRC32)
    return uncompressedData;

  std::cout << std::format("File: {} CRC32 mismatch! Expected: {} Found: {}\n", fileName, node->fileHeader.CRC32, crc);
  return nullptr;
}

void MZip::extractFile(std::string_view fileName, const std::filesystem::path &extractPath)
{
  const auto *node = ArchiveTree->findFileNode(std::string(fileName));
  if (!node)
    return;

  auto file = GetFile(fileName);
  if (!file)
    return;

  auto destPath = extractPath;
  if (std::filesystem::is_directory(destPath))
  {
    destPath /= std::filesystem::path(fileName).filename();
  }

  if (std::filesystem::exists(destPath))
    return; // File already exists, skip extraction

  std::filesystem::create_directories(destPath.parent_path());

  std::ofstream outFile(destPath, std::ios::binary);
  outFile.write(file.get(), node->fileHeader.UncompressedSize);
}

void MZip::extractFiles(const std::vector<std::string> &files, const std::filesystem::path &extractPath)
{
  // Extract files directly, preserving directory structure
  for (const auto &file : files)
  {
    const auto *node = ArchiveTree->findFileNode(file);
    if (node)
    {
      auto destPath = extractPath / std::filesystem::path(file);
      extractFile(file, destPath);
    }
  }
}

void MZip::extractDirectory(std::string_view dirPath, const std::filesystem::path &extractPath)
{
  const auto *node = ArchiveTree->lookup(std::string(dirPath));
  if (!node || !node->isDirectory)
    return;

  auto basePath = extractPath;
  std::filesystem::create_directories(basePath);

  // Recursive function to extract files directly as we traverse
  std::function<void(const std::string &)> extractRecursive = [&](const std::string &currentPath)
  {
    const ZipNode *currentNode = ArchiveTree->lookup(currentPath);
    if (!currentNode)
      return;

    if (!currentNode->isDirectory)
    {
      // Extract file directly
      auto destPath = basePath / std::filesystem::path(currentPath);
      extractFile(currentPath, destPath);
    }
    else
    {
      // Create directory and recurse into children
      std::filesystem::create_directories(basePath / currentPath);
      auto children = ArchiveTree->getChildren(currentPath);
      for (const auto &child : children)
      {
        std::string childPath = currentPath.empty() ? child : currentPath + "/" + child;
        extractRecursive(childPath);
      }
    }
  };

  extractRecursive(std::string(dirPath));
}

void MZip::extractArchive(std::string_view path)
{
  auto extractPath = std::filesystem::path(path).parent_path() / std::filesystem::path(path).stem();
  extractDirectory("", extractPath);
}

//------------------------------------------------------------------------------
// ZIP header operations
//------------------------------------------------------------------------------

zip::EndOfCentralDirectoryRecord MZip::getEndRecord()
{
  zip::EndOfCentralDirectoryRecord dirEnd{};
  archiveFile->seekg(-sizeof(dirEnd), std::ios::end);
  fetchHeaderData(&dirEnd);
  return dirEnd;
}

zip::CentralDirectoryFileHeader MZip::getCentralHeader()
{
  zip::CentralDirectoryFileHeader dirHeader{};
  fetchHeaderData(&dirHeader);
  return dirHeader;
}

std::string MZip::getNextHeaderString(std::size_t length)
{
  std::string str(length, '\0');
  fetchHeaderData(str.data(), length);
  return str;
}

template <typename T> bool MZip::checkSignature(T &_struct)
{
  if constexpr (std::is_same_v<T, zip::LocalFileHeader>)
  {
    if (_version == mzip::Version::Mrs1)
      return _struct.Signature == mzip::v1::LocalFileHeaderSignature ||
             _struct.Signature == mzip::v1::LocalFileHeaderSignature2;
    else if (_version == mzip::Version::Mrs2)
      return _struct.Signature == mzip::v2::LocalFileHeaderSignature;
    else
      return false;
  }
  else if constexpr (std::is_same_v<T, zip::CentralDirectoryFileHeader>)
  {
    if (_version == mzip::Version::Mrs1)
      return _struct.Signature == mzip::v1::CentralDirectorySignature;
    else if (_version == mzip::Version::Mrs2)
      return _struct.Signature == mzip::v2::CentralDirectorySignature;
    else
      return false;
  }
  else if constexpr (std::is_same_v<T, zip::EndOfCentralDirectoryRecord>)
  {
    if (_version == mzip::Version::Mrs1)
      return _struct.Signature == mzip::v1::CentralDirectoryEndSignature ||
             _struct.Signature == mzip::v1::CentralDirectoryEndSignature2;
    else if (_version == mzip::Version::Mrs2)
      return _struct.Signature == mzip::v2::CentralDirectoryEndSignature ||
             _struct.Signature == mzip::v2::CentralDirectoryEndSignature2;
    else
      return false;
  }
  return false;
}

bool MZip::buildArchiveTree(zip::EndOfCentralDirectoryRecord dirEnd)
{
  ArchiveTree = std::make_shared<ZipTree>();

  for (uint16_t i = 0; i < dirEnd.DirectoryCountOnDisk; ++i)
  {
    auto dirHeader = getCentralHeader();

    if (!checkSignature(dirHeader))
      continue;

    auto FileName = getNextHeaderString(dirHeader.FileNameLength);

    // Insert file with its ZIP header directly
    ArchiveTree->insert(FileName, dirHeader);

    archiveFile->seekg(dirHeader.ExtraFieldLength + dirHeader.CommentLength, std::ios::cur);
  }

  return true;
}

//------------------------------------------------------------------------------
// ZIP structure helpers
//------------------------------------------------------------------------------

zip::LocalFileHeader MZip::makeLocalHeader(const zip::CentralDirectoryFileHeader &central)
{
  return {.Signature = zip::Signature,
          .Version = central.Version,
          .Flags = central.BitFlag,
          .Compression = central.CompressionMethod,
          .LastModified = central.LastModified,
          .CRC32 = central.CRC32,
          .CompressedSize = central.CompressedSize,
          .UncompressedSize = central.UncompressedSize,
          .FileNameLength = central.FileNameLength,
          .ExtraFieldLength = central.ExtraFieldLength};
}

zip::CentralDirectoryFileHeader MZip::makeCentralHeader(DOSDateTime modified, uint32_t crc, uint32_t compSize,
                                                        uint32_t uncompSize, uint16_t nameLen, uint32_t offset)
{
  return {.Signature = mzip::v2::CentralDirectorySignature,
          .Version = 25,
          .MinVersion = 20,
          .BitFlag = 0,
          .CompressionMethod = 8,
          .LastModified = modified,
          .CRC32 = crc,
          .CompressedSize = compSize,
          .UncompressedSize = uncompSize,
          .FileNameLength = nameLen,
          .ExtraFieldLength = 0,
          .CommentLength = 0,
          .DiskStartNum = 0,
          .InternalFileAttributes = 0,
          .ExternalFileAttributes = 0,
          .FileHeaderOffset = offset};
}

zip::EndOfCentralDirectoryRecord MZip::makeCentralEnd(uint16_t fileCount, uint32_t dirSize, uint32_t dirOffset)
{
  return {.Signature = mzip::v2::CentralDirectoryEndSignature,
          .DiskNumber = 0,
          .DiskStartNumber = 0,
          .DirectoryCountOnDisk = fileCount,
          .DirectoryCountTotal = fileCount,
          .CentralDirectorySize = dirSize,
          .CentralDirectoryOffset = dirOffset,
          .CommentLength = 0};
}

//------------------------------------------------------------------------------
// Data processing
//------------------------------------------------------------------------------

void MZip::ConvertChar(std::span<char> data, bool recover)
{
  for (char &c : data)
  {
    std::uint8_t b = recover ? c : c ^ 0xFF;
    std::uint8_t result = recover ? ((b >> 3) | (b << 5)) ^ 0xFF : ((b << 3) | (b >> 5));
    c = static_cast<char>(result & 0xFF);
  }
}

template <typename T> void MZip::fetchHeaderData(T *data, std::optional<std::size_t> size)
{
  const auto dataSize = size.value_or(sizeof(*data));

  archiveFile->read(reinterpret_cast<char *>(data), dataSize);
  if (_version == mzip::Version::Mrs2)
  {
    ConvertChar({reinterpret_cast<char *>(data), dataSize}, true);
  }
}

template <typename T> void MZip::writeHeaderData(std::ofstream &file, T *data, std::optional<std::size_t> size)
{
  const auto dataSize = size.value_or(sizeof(*data));
  if (_version == mzip::Version::Mrs2)
  {
    ConvertChar({reinterpret_cast<char *>(data), dataSize}, false);
  }
  file.write(reinterpret_cast<char *>(data), dataSize);
}

uint32_t MZip::processData(std::span<char> inData, std::span<char> outData, bool compress)
{
  z_stream stream{};
  stream.next_in = reinterpret_cast<Bytef *>(inData.data());
  stream.avail_in = inData.size();
  stream.next_out = reinterpret_cast<Bytef *>(outData.data());
  stream.avail_out = outData.size();

  int result = compress ? deflate(stream) : inflate(stream);
  if (result == Z_STREAM_END)
    return crc32(0L, reinterpret_cast<Bytef *>(outData.data()), outData.size());

  return 0;
}

//------------------------------------------------------------------------------
// Compression helpers
//------------------------------------------------------------------------------

int MZip::processStream(z_stream &stream, int (*init)(z_stream *), int (*process)(z_stream *, int), int (*end)(z_stream *))
{
  if (init(&stream) != Z_OK)
  {
    end(&stream);
    return Z_ERRNO;
  }
  int status = process(&stream, Z_FINISH);
  end(&stream);
  return status;
}

int MZip::deflate(z_stream &stream)
{
  auto init = [](z_stream *s)
  { return deflateInit2(s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY); };
  return processStream(stream, init, ::deflate, deflateEnd);
}

int MZip::inflate(z_stream &stream)
{
  auto init = [](z_stream *s) { return inflateInit2(s, -MAX_WBITS); };
  return processStream(stream, init, ::inflate, inflateEnd);
}
