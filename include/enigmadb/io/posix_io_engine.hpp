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

    enigmadb::common::ExpectResult<FileHandle, enigmadb::common::Error> open(
        const std::string& path, Mode mode) override;

    enigmadb::common::ExpectResult<size_t, enigmadb::common::Error> append(
        const FileHandle& fh, const uint8_t* buffer, size_t length) override;

    enigmadb::common::ExpectResult<size_t, enigmadb::common::Error> read(
        const FileHandle& fh, size_t count, uint8_t* buffer,
        size_t offset) override;

    enigmadb::common::ExpectResult<void, enigmadb::common::Error> sync_all(
        const FileHandle& fh) override;

    enigmadb::common::ExpectResult<void, enigmadb::common::Error>
    sync_directory(const std::string& path) override;

    enigmadb::common::ExpectResult<void, enigmadb::common::Error> sync_data(
        const FileHandle& fh) override;
};

}  // namespace enigmadb::io

#endif  // ENIGMADB_POSIX_IO_ENGINE_HPP
