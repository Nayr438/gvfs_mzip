#include "MZip.h"
#include "MZFile.h"
#include "MZipConstants.h"
#include "ZipStructs.h"
#include "ZipTrie.h"
#include <algorithm>
#include <cstring>
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
  archiveFile = std::make_unique<MZFile>(archivePath);

  if (!archiveFile->is_open())
    return false;

  version(archiveFile->getSignature());

  auto dirEnd = getEndRecord();
  archiveFile->seek(dirEnd.CentralDirectoryOffset, std::ios::beg);

  return buildArchiveTree(dirEnd);
}

//------------------------------------------------------------------------------
// File operations
//------------------------------------------------------------------------------

std::shared_ptr<char[]> MZip::GetFile(std::string_view fileName)
{
  const auto *node = ArchiveTree->lookup(fileName);
  if (!node || !node->isFile())
    return nullptr;

  // Read local header
  archiveFile->seek(node->fileData->FileHeaderOffset, std::ios::beg);

  zip::LocalFileHeader header{};
  fetchHeaderData(&header);

  if (!checkSignature(header))
    return nullptr;

  // Skip name and extra fields
  archiveFile->seek(header.FileNameLength + header.ExtraFieldLength, std::ios::cur);

  auto uncompressedData = std::make_shared<char[]>(node->fileData->UncompressedSize);

  if (node->fileData->CompressedSize == node->fileData->UncompressedSize)
  {
    archiveFile->read(uncompressedData.get(), node->fileData->UncompressedSize);
    return uncompressedData;
  }

  auto compressedData = std::make_unique<char[]>(node->fileData->CompressedSize);

  archiveFile->read(compressedData.get(), node->fileData->CompressedSize);

  // auto crc =
  auto crc = processData(std::span{compressedData.get(), node->fileData->CompressedSize},
                         std::span{uncompressedData.get(), node->fileData->UncompressedSize}, false);

  if (crc == node->fileData->CRC32)
    return uncompressedData;

  std::cout << "CRC32 mismatch!\n";
  return nullptr;
}

void MZip::extractFile(std::string_view fileName, const std::filesystem::path &extractPath)
{
  auto file = GetFile(fileName);
  if (!file)
    return; // More idiomatic nullptr check

  const auto *node = ArchiveTree->lookup(fileName);
  if (!node || !node->isFile())
    return;

  auto destPath = extractPath;
  if (std::filesystem::is_directory(destPath))
  {
    destPath /= std::filesystem::path(fileName).filename();
  }

  if (std::filesystem::exists(destPath))
    return;

  std::filesystem::create_directories(destPath.parent_path());

  std::ofstream outFile(destPath, std::ios::binary);
  outFile.write(file.get(), node->fileData->UncompressedSize);
}

void MZip::extractFiles(const std::vector<std::string_view> &files, const std::filesystem::path &extractPath)
{
  // Collect all valid files to extract
  std::vector<std::pair<std::string_view, std::filesystem::path>> extractTasks;
  for (const auto &file : files)
  {
    const auto *node = ArchiveTree->lookup(file);
    if (node && node->isFile())
    {
      auto destPath = extractPath / std::filesystem::path(file).filename();
      extractTasks.emplace_back(file, destPath);
    }
  }

  processExtractionTasks(extractTasks);
}

void MZip::assignExtractTasks(TaskVector &extractTasks, const std::filesystem::path &filePath,
                              const std::filesystem::path &basePath)
{
  const auto *node = ArchiveTree->lookup(filePath);
  if (!node)
    return;

  if (node->isFile())
  {
    extractTasks.emplace_back(filePath.string(), basePath / std::filesystem::path(filePath));
  }
  else
  {
    std::filesystem::create_directories(basePath / filePath);
  }
}

void MZip::extractDirectory(std::string_view dirPath, const std::filesystem::path &extractPath)
{
  const auto *node = ArchiveTree->lookup(dirPath);
  if (!node || node->isFile())
    return;

  auto basePath = extractPath;
  std::filesystem::create_directories(basePath);

  TaskVector extractTasks;

  ArchiveTree->traverse(dirPath, [&](const std::filesystem::path &path, const ZipTrieNode &)
                        { assignExtractTasks(extractTasks, path, basePath); });

  processExtractionTasks(extractTasks);
}

void MZip::extractArchive(std::string_view path)
{
  auto extractPath = std::filesystem::path(path).parent_path() / std::filesystem::path(path).stem();
  extractDirectory("", extractPath);
}

//------------------------------------------------------------------------------
// Archive creation and modification
//------------------------------------------------------------------------------

void MZip::createArchive(const std::filesystem::path &path, int version)
{
  std::ofstream archive(path, std::ios::binary);
  if (!archive)
    return;

  mzip::Version ver = static_cast<mzip::Version>(version);
  uint32_t headerOffset = 0;
  std::vector<zip::CentralDirectoryFileHeader> centralHeaders;

  ArchiveTree->traverse("",
                        [&](const std::filesystem::path &path, const ZipTrieNode &node)
                        {
                          if (!node.isFile())
                            return;

                          auto central = makeCentralHeader(node.fileData->LastModified, node.fileData->CRC32,
                                                           node.fileData->CompressedSize, node.fileData->UncompressedSize,
                                                           path.string().length(), headerOffset);

                          auto local = makeLocalHeader(central);
                          if (ver == mzip::Version::Mrs2)
                            ConvertChar(std::span{reinterpret_cast<char *>(&local), sizeof(local)}, true);

                          archive.write(reinterpret_cast<char *>(&local), sizeof(local));
                          archive.write(path.string().c_str(), path.string().length());

                          headerOffset = static_cast<uint32_t>(archive.tellp());
                          centralHeaders.push_back(central);
                        });

  uint32_t dirOffset = static_cast<uint32_t>(archive.tellp());
  for (auto &header : centralHeaders)
  {
    if (ver == mzip::Version::Mrs2)
    {
      ConvertChar(std::span{reinterpret_cast<char *>(&header), sizeof(header)}, true);
    }
    archive.write(reinterpret_cast<char *>(&header), sizeof(header));
  }

  uint32_t dirSize = static_cast<uint32_t>(archive.tellp()) - dirOffset;
  auto end = makeCentralEnd(centralHeaders.size(), dirSize, dirOffset);

  if (ver == mzip::Version::Mrs2)
  {
    ConvertChar(std::span{reinterpret_cast<char *>(&end), sizeof(end)}, true);
  }

  archive.write(reinterpret_cast<char *>(&end), sizeof(end));
}

bool MZip::createEmpty(const std::filesystem::path &path, mzip::Version version)
{
  std::ofstream archive(path, std::ios::binary);
  if (!archive)
    return false;

  // Write empty central directory end
  auto end = makeCentralEnd(0, 0, 0);
  if (version == mzip::Version::Mrs2)
  {
    ConvertChar(std::span{reinterpret_cast<char *>(&end), sizeof(end)}, true);
  }
  archive.write(reinterpret_cast<char *>(&end), sizeof(end));

  archivePath = path;
  return true;
}

//------------------------------------------------------------------------------
// ZIP header operations
//------------------------------------------------------------------------------

zip::EndOfCentralDirectoryRecord MZip::getEndRecord()
{
  zip::EndOfCentralDirectoryRecord dirEnd{};
  archiveFile->seek(-sizeof(dirEnd), std::ios::end);
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
    return _struct.Signature == mzip::LocalFileHeaderSignature;
  else if constexpr (std::is_same_v<T, zip::CentralDirectoryFileHeader>)
    return _struct.Signature == mzip::CentralDirectorySignature;
  else if constexpr (std::is_same_v<T, zip::EndOfCentralDirectoryRecord>)
    return _struct.Signature == zip::Signature || _struct.Signature == mzip::CentralDirectorySignature;
  return false;
}

mzip::Version MZip::version(std::optional<std::uint32_t> signature)
{
  if (signature.has_value())
    _version = (signature == mzip::LocalFileHeaderSignature) ? mzip::Version::Mrs1 : mzip::Version::Mrs2;

  return _version;
}

bool MZip::buildArchiveTree(zip::EndOfCentralDirectoryRecord dirEnd)
{
  ArchiveTree = std::make_shared<ZipTrie>();

  for (uint16_t i = 0; i < dirEnd.DirectoryCountOnDisk; ++i)
  {
    auto dirHeader = getCentralHeader();

    if (!checkSignature(dirHeader))
      continue;

    auto FileName = getNextHeaderString(dirHeader.FileNameLength);

    // Insert file with its path and data
    ArchiveTree->insert(FileName, toNodeFileHeader(dirHeader));

    archiveFile->seek(dirHeader.ExtraFieldLength + dirHeader.CommentLength, std::ios::cur);
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
  return {.Signature = mzip::CentralDirectorySignature,
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
  return {.Signature = mzip::CentralDirectoryEndSignature,
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

  archiveFile->read(data, dataSize);
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

  if (compress ? deflate(stream) : inflate(stream) == Z_STREAM_END)
    return crc32(0L, reinterpret_cast<Bytef *>(outData.data()), outData.size());

  return 0;
}
void MZip::processExtractionTasks(const std::vector<std::pair<std::string_view, std::filesystem::path>> &tasks)
{
  // Process files in parallel
  std::vector<std::future<void>> futures;
  for (const auto &[name, destPath] : tasks)
  {
    futures.push_back(
        std::async(std::launch::async, [this, name = name, destPath = destPath]() { extractFile(name, destPath); }));
  }

  // Wait for all extractions to complete
  for (auto &future : futures)
  {
    future.wait();
  }
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
