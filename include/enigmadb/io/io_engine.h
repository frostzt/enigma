/*
 * @file io_engine.hpp
 * @brief Abstract IO engine interface for EnigmaDB storage layer.
 * @author frostzt
 * @date 2026-03-24
 */

#ifndef ENIGMADB_IO_ENGINE_H
#define ENIGMADB_IO_ENGINE_H

#include <fcntl.h>
#include <sys/fcntl.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <ostream>
#include <string>

#include "enigmadb/base.h"

namespace enigmadb::io {

enum class Mode {
    READ,       /// O_RDONLY
    WRITE,      /// O_WRONLY | O_CREAT
    READWRITE,  /// O_RDWR   | O_CREAT
    APPEND,     /// O_WRONLY | O_APPEND | O_CREAT
    OVERWRITE,  /// O_WRONLY | O_CREAT | O_TRUNC
};

inline std::ostream& operator<<(std::ostream& out, const Mode& mode) {
    switch (mode) {
            // clang-format off
            case Mode::READ:      out << "O_RDONLY";                            break;
            case Mode::WRITE:     out << "O_WRONLY | O_CREAT";                  break;
            case Mode::READWRITE: out << "O_RDWR | O_CREAT";                    break;
            case Mode::APPEND:    out << "O_WRONLY | O_APPEND | O_CREAT";       break;
            case Mode::OVERWRITE: out << "O_WRONLY | O_CREAT | O_TRUNC";        break;
            default:              out << "UNKNOWN_MODE";                        break;
            // clang-format on
    }
    return out;
}

/**
 * @brief Owns a file descriptor with RAII lifetime semantics.
 *
 * FileHandle is move-only. The descriptor is closed on destruction.
 * Obtain instances exclusively through IOEngine::open().
 */
class FileHandle {
   private:
    int fd_;

   public:
    struct ConstructTag {
        ConstructTag() = default;
        ConstructTag(const ConstructTag&) = default;
        ConstructTag(ConstructTag&&) = default;
        ConstructTag& operator=(const ConstructTag&) = default;
        ConstructTag& operator=(ConstructTag&&) = default;
        ~ConstructTag() = default;
    };
    FileHandle(ConstructTag, int fd) : fd_(fd) {}

    ~FileHandle() {
        if (fd_ != -1) {
            close(fd_);
        }
    }

    FileHandle(const FileHandle& other) = delete;
    FileHandle& operator=(const FileHandle& other) = delete;

    friend class IOEngine;

    /* Returns internal file descriptor */
    int fd() const { return fd_; }

    FileHandle& operator=(FileHandle&& other) noexcept {
        /* Release current fd before we take ownership of the other fd */
        if (fd_ != -1) {
            close(fd_);
        }
        /* Take the new fd */
        this->fd_ = other.fd_;
        other.fd_ = -1;
        return *this;
    }

    FileHandle(FileHandle&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; };
};

class IOEngine {
   public:
    IOEngine(const IOEngine&) = default;
    IOEngine(IOEngine&&) = delete;
    IOEngine& operator=(const IOEngine&) = default;
    IOEngine& operator=(IOEngine&&) = delete;
    virtual ~IOEngine() = default;

    /**
     * @brief Tries to open a file specified at @p path with @p mode specified
     *
     * @param path Path to the file to open
     * @param mode Specifies the mode in which to open this file
     * @return A FileHandle which manages the file opened otherwise Error
     */
    virtual Result<FileHandle> open(const std::string& path, Mode mode) = 0;

    /* @brief Tries to append data directly into the file handled by @p fh
     *
     * @param fh       Handle to an open file (must have been opened with
     * Mode::Read).param
     * @param[out] buffer Destination buffer; caller-owned
     * @param length Length of bytes to append to the file
     * @return Number of bytes appended, or Error on failure.
     */
    virtual Result<size_t> append(const FileHandle& fh, const uint8_t* buffer, size_t length) = 0;

    virtual Result<size_t> file_size(const FileHandle& fh) = 0;

    /**
     * @brief Reads up to @p count bytes from @p fh at the given @p offset.
     *
     * @param fh       Handle to an open file (must have been opened with
     * Mode::Read).
     * @param count    Maximum number of bytes to read.
     * @param[out] buffer     Destination buffer; caller-owned.
     * @param offset   Byte offset from the start of the file.
     * @return Number of bytes actually read, or Error on failure.
     */
    virtual Result<size_t> read(const FileHandle& fh, size_t count, uint8_t* buffer, size_t offset) = 0;

    /**
     * @brief Flushes all the directory and flushes it to the disk
     *
     * @param fh       Handle to an open file (must have been opened with
     * Mode::Read).
     * @return void is successful, or Error on failure.
     */
    virtual Result<void> sync_directory(const std::string& path) = 0;

    /**
     * @brief Flushes all the changes to the disk similar to fsync
     *
     * @param fh       Handle to an open file (must have been opened with
     * Mode::Read).
     * @return void is successful, or Error on failure.
     */
    virtual Result<void> sync_all(const FileHandle& fh) = 0;

    /**
     * @brief Flushes all the data changes except metadata* to the disk
     * similar to fdatasync
     *
     * Note that in case of appends fdatasync still guarantees filesize
     * will be comitted.
     *
     * @param fh       Handle to an open file (must have been opened with
     * Mode::Read).
     * @return void is successful, or Error on failure.
     */
    virtual Result<void> sync_data(const FileHandle& fh) = 0;

    /**
     * @brief Cleanly unlinks the provided file and syncs the parent directory
     *
     *
     * @param path       Path to the file to be removed
     * @return void is successful, or Error on failure.
     */
    virtual Result<void> remove(const std::string& path) = 0;
};

}  // namespace enigmadb::io

template <>
struct std::formatter<enigmadb::io::Mode> : std::formatter<std::string_view> {
    auto format(enigmadb::io::Mode mode, format_context& ctx) const {
        // clang-format off
        std::string_view name = "UNKNOWN_MODE";
        switch (mode) {
            case enigmadb::io::Mode::READ:      name = "O_RDONLY";                      break;
            case enigmadb::io::Mode::WRITE:     name = "O_WRONLY | O_CREAT";            break;
            case enigmadb::io::Mode::READWRITE: name = "O_RDWR | O_CREAT";              break;
            case enigmadb::io::Mode::APPEND:    name = "O_WRONLY | O_APPEND | O_CREAT"; break;
            case enigmadb::io::Mode::OVERWRITE: name = "O_WRONLY | O_CREAT | O_TRUNC";  break;
        }
        return std::formatter<std::string_view>::format(name, ctx);
        // clang-format on
    }
};

#endif  // ENIGMADB_IO_ENGINE_H
