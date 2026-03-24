/*
 * @file io_engine.hpp
 * @brief Abstract IO engine interface for EnigmaDB storage layer.
 * @author frostzt
 * @date 2026-03-24
 */

#ifndef ENIGMADB_IO_ENGINE_HPP
#define ENIGMADB_IO_ENGINE_HPP

#include <cstddef>
#include <fcntl.h>
#include <string>
#include <sys/fcntl.h>
#include <unistd.h>

#include "enigmadb/common/error.h"
#include "enigmadb/common/result.h"

namespace enigmadb::io {

enum class Mode {
  Read,     /// O_RDONLY
  Write,    /// O_WRONLY | O_CREAT
  Append,   /// O_WRONLY | O_APPEND | O_CREAT
  Overwrite /// O_WRONLY | O_CREAT | O_TRUNC
};

/**
 * @brief Owns a file descriptor with RAII lifetime semantics.
 *
 * FileHandle is move-only. The descriptor is closed on destruction.
 * Obtain instances exclusively through IOEngine::open().
 */
class FileHandle {
private:
  int _fd;

  ~FileHandle() {
    if (_fd != -1) {
      close(_fd);
    }
  }

public:
  struct __tag__ {};
  FileHandle(__tag__, int fd) : _fd(fd) {}

  FileHandle(const FileHandle &other) = delete;
  FileHandle &operator=(const FileHandle &other) = delete;

  friend class IOEngine;

  /* Returns internal file descriptor */
  int fd() const { return _fd; }

  FileHandle &operator=(FileHandle &&other) noexcept {
    /* Release current fd before we take ownership of the other fd */
    if (_fd != -1) {
      close(_fd);
    }
    /* Take the new fd */
    this->_fd = other._fd;
    other._fd = -1;
    return *this;
  }

  FileHandle(FileHandle &&other) noexcept : _fd(other._fd) { other._fd = -1; };
};

class IOEngine {
public:
  inline ~IOEngine() = default;

  /**
   * @brief Tries to open a file specified at @p path with @p mode specified
   *
   * @param path Path to the file to open
   * @param mode Specifies the mode in which to open this file
   * @return A FileHandle which manages the file opened otherwise Error
   */
  virtual ExpectResult<FileHandle, Error> open(const std::string &path,
                                               Mode mode) = 0;

  /* @brief Tries to append data directly into the file handled by @p fh
   *
   * @param fh       Handle to an open file (must have been opened with
   * Mode::Read).param
   * @param[out] buffer Destination buffer; caller-owned
   * @param buffer_len Size of @p buffer in bytes
   * @param length Length of bytes to append to the file
   * @return Number of bytes appended, or Error on failure.
   */
  virtual ExpectResult<size_t, Error> append(const FileHandle &fh, char *buffer,
                                             size_t buffer_len,
                                             size_t length) = 0;

  /**
   * @brief Reads up to @p count bytes from @p fh at the given @p offset.
   *
   * @param count    Maximum number of bytes to read.
   * @param fh       Handle to an open file (must have been opened with
   * Mode::Read).
   * @param[out] buffer     Destination buffer; caller-owned.
   * @param buffer_len     Size of @p buffer in bytes. Must be >= @p count.
   * @param offset   Byte offset from the start of the file.
   * @return Number of bytes actually read, or Error on failure.
   */
  virtual ExpectResult<size_t, Error> read(size_t count, const FileHandle &fh,
                                           char *buffer, size_t buffer_len,
                                           size_t offset) = 0;

  /**
   * @brief Commits all the changes into disk
   */
  virtual ExpectResult<void, Error> sync(const FileHandle &fh) = 0;
};

} // namespace enigmadb::io

#endif // ENIGMADB_IO_ENGINE_HPP
