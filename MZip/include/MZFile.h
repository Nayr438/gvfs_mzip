#pragma once

#include <cstdint>
#include <fstream>
#include <type_traits>

class MZFile
{
public:
  enum class mode
  {
    Read = std::ios::in | std::ios::binary,
    Write = std::ios::out | std::ios::binary,
    ReadWrite = std::ios::in | std::ios::out | std::ios::binary
  };

  MZFile() = default;
  template <typename T> MZFile(const T &path, mode openMode = mode::Read) { open(path, openMode); }
  ~MZFile() = default;

  MZFile(MZFile &&) noexcept = default;
  MZFile &operator=(MZFile &&) noexcept = default;
  MZFile(const MZFile &) = delete;
  MZFile &operator=(const MZFile &) = delete;

  template <typename T> bool open(const T &path, mode openMode = mode::Read)
  {
    if constexpr (std::is_same_v<T, const char *> || std::is_same_v<T, char *>)
      file_.open(path, static_cast<std::ios::openmode>(openMode));
    else
      file_.open(path.c_str(), static_cast<std::ios::openmode>(openMode));
    return file_.is_open();
  }

  void close() { file_.close(); }
  bool is_open() const { return file_.is_open(); }
  bool good() const { return file_.good(); }
  void clear() { file_.clear(); }

  template <typename T> void read(T *value, std::streamsize size = sizeof(T))
  {
    file_.read(reinterpret_cast<char *>(value), size);
  }

  template <typename T> void write(const T *value, std::streamsize size = sizeof(T))
  {
    file_.write(reinterpret_cast<const char *>(value), size);
  }

  void seek(std::streampos pos, std::ios::seekdir direction = std::ios::cur) { file_.seekg(pos, direction); }
  std::streampos tellg() { return file_.tellg(); }

  std::uint32_t getSignature() noexcept;

private:
  std::fstream file_;
};
