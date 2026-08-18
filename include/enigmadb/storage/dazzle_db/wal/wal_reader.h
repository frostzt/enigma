/**
 * @file wal_reader.h
 * @brief WAL Reader
 *
 * @author frostzt
 * @date 2026-04-03
 */

#ifndef ENIGMA_DB_WAL_READER_H
#define ENIGMA_DB_WAL_READER_H

#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/dazzle_db/wal/wal_record.h"

namespace enigmadb::dazzle {

class WalReader {
   private:
    std::string path_;
    io::FileHandle fh_;
    io::IOEngine& engine_;
    size_t offset_;

    WalReader(const std::string& path, io::FileHandle fh, io::IOEngine& engine)
        : path_(path), fh_(std::move(fh)), engine_(engine), offset_(0) {}

   public:
    Result<WalRecord> next();

    static Result<WalReader> create(io::IOEngine& engine, const std::string& path);
};

}  // namespace enigmadb::dazzle

#endif  // ENIGMA_DB_WAL_READER_H
