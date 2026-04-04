/*
 * @file posix_io_engine.hpp
 * @brief IO engine implementation for POSIX
 * @author frostzt
 * @date 2026-03-25
 */

#ifndef ENIGMADB_POSIX_IO_ENGINE_H
#define ENIGMADB_POSIX_IO_ENGINE_H

#include <fcntl.h>
#include <sys/fcntl.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "enigmadb/io/io_engine.h"

namespace enigmadb::io {

class PosixIOEngine : public IOEngine {
   public:
    ~PosixIOEngine() {};

    IOResult<FileHandle> open(const std::string& path, Mode mode) override;

    IOResult<size_t> append(const FileHandle& fh, const uint8_t* buffer,
                            size_t length) override;

    IOResult<size_t> read(const FileHandle& fh, size_t count, uint8_t* buffer,
                          size_t offset) override;

    IOResult<void> sync_all(const FileHandle& fh) override;

    IOResult<void> sync_directory(const std::string& path) override;

    IOResult<void> sync_data(const FileHandle& fh) override;
};

}  // namespace enigmadb::io

#endif  // ENIGMADB_POSIX_IO_ENGINE_H
