/*
 * @file posix_io_engine.hpp
 * @brief IO engine implementation for POSIX
 * @author frostzt
 * @date 2026-03-25
 */

#ifndef ENIGMADB_POSIX_IO_ENGINE_HPP
#define ENIGMADB_POSIX_IO_ENGINE_HPP

#include <fcntl.h>
#include <sys/fcntl.h>
#include <unistd.h>

#include <cstddef>
#include <string>

#include "enigmadb/io/io_engine.hpp"

namespace enigmadb::io {

class PosixIOEngine : public IOEngine {
   public:
    ~PosixIOEngine() {};

    ExpectResult<FileHandle, Error> open(const std::string& path,
                                         Mode mode) override;

    ExpectResult<size_t, Error> append(const FileHandle& fh, const char* buffer,
                                       size_t length) override;

    ExpectResult<size_t, Error> read(const FileHandle& fh, size_t count,
                                     char* buffer, size_t offset) override;

    ExpectResult<void, Error> sync_all(const FileHandle& fh) override;

    ExpectResult<void, Error> sync_directory(const std::string& path) override;

    ExpectResult<void, Error> sync_data(const FileHandle& fh) override;
};

}  // namespace enigmadb::io

#endif  // ENIGMADB_POSIX_IO_ENGINE_HPP
