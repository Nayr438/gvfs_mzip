#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "MZFile.h"
#include "MZipConstants.h"
#include "ZipStructs.h"
#include "ZipTrie.h"
#include "zlib.h"

class MZipRecovery;

class MZip
{
  friend class MZipRecovery;
public:
  explicit MZip(std::string_view fileName);
  virtual ~MZip() = default;

  // Core operations
  bool openArchive();
  virtual bool openArchiveForced();

  // File operations
  std::shared_ptr<char[]> GetFile(std::string_view fileName);
  void extractFile(std::string_view fileName, const std::filesystem::path &extractPath);
  void extractFiles(const std::vector<std::string_view> &files, const std::filesystem::path &extractPath);
  void extractDirectory(std::string_view dirPath, const std::filesystem::path &extractPath);
  void extractArchive(std::string_view path);

  // Archive creation and modification
  void createArchive(const std::filesystem::path &path, int version);
  bool createEmpty(const std::filesystem::path &path, mzip::Version version);
  bool addFile(const std::filesystem::path &filePath, std::string_view archivePath);
  bool addDirectory(const std::filesystem::path &dirPath, std::string_view archivePath = "");
  bool removeFile(std::string_view fileName);

  // Accessors
  const std::shared_ptr<ZipTrie> &getTree() const noexcept { return ArchiveTree; }
  const std::filesystem::path &getPath() const noexcept { return archivePath; }

private:
  // Core data members
  std::shared_ptr<ZipTrie> ArchiveTree;
  std::filesystem::path archivePath;
  mzip::Version _version;
  std::unique_ptr<MZFile> archiveFile;
  bool recovery = false;

  using TaskVector = std::vector<std::pair<std::string_view, std::filesystem::path>>;

  void assignExtractTasks(TaskVector &extractTasks, const std::filesystem::path &filePath, const std::filesystem::path &basePath);

  // ZIP header operations
  inline zip::EndOfCentralDirectoryRecord getEndRecord();
  inline zip::CentralDirectoryFileHeader getCentralHeader();
  inline std::string getNextHeaderString(std::size_t length);
  template <typename T> inline bool checkSignature(T &_struct);
  inline mzip::Version version(std::optional<std::uint32_t> signature);
  inline bool buildArchiveTree(zip::EndOfCentralDirectoryRecord dirEnd);

  // ZIP structure helpers
  zip::LocalFileHeader makeLocalHeader(const zip::CentralDirectoryFileHeader &central);
  zip::CentralDirectoryFileHeader makeCentralHeader(DOSDateTime modified, uint32_t crc, uint32_t compSize,
                                                    uint32_t uncompSize, uint16_t nameLen, uint32_t offset);
  zip::EndOfCentralDirectoryRecord makeCentralEnd(uint16_t fileCount, uint32_t dirSize, uint32_t dirOffset);

  // Data processing
  template <typename T> void fetchHeaderData(T *data, std::optional<std::size_t> size = std::nullopt);
  template <typename T> void writeHeaderData(std::ofstream &file, T *data, std::optional<std::size_t> size = std::nullopt);
  uint32_t processData(std::span<char> inData, std::span<char> outData, bool compress);
  void ConvertChar(std::span<char> data, bool recover);

  void processExtractionTasks(const std::vector<std::pair<std::string_view, std::filesystem::path>> &tasks);

  // File signature mapping
  const std::map<std::uint32_t, std::string_view> signatureMap = {{0x20000, ".tga"},
                                                                  {0x107f060, ".elu"},
                                                                  {0x235849298, ".rs.bsp"},
                                                                  {0x5050178f, ".rs.col"},
                                                                  {0x330671804, ".rs.lm"},
                                                                  {0xe11ab1a1e011cfd0, "_thumbs.db"},
                                                                  {0x464a1000e0ffd8ff, ".jpg"},
                                                                  {0xa1a0a0d474e5089, ".png"},
                                                                  {0x7c20534444, ".dds"}};

  // Compression helpers
  int processStream(z_stream &stream, int (*init)(z_stream *), int (*process)(z_stream *, int), int (*end)(z_stream *));
  int deflate(z_stream &stream);
  int inflate(z_stream &stream);
};
