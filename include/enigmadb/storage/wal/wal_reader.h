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
#include "enigmadb/storage/wal/wal_record.h"

namespace enigmadb::storage::wal {

class WalReader {
   private:
    std::string path_;
    enigmadb::io::FileHandle fh_;
    enigmadb::io::IOEngine& engine_;
    size_t offset_;

    WalReader(const std::string& path, enigmadb::io::FileHandle fh,
              enigmadb::io::IOEngine& engine)
        : path_(path), fh_(std::move(fh)), engine_(engine), offset_(0) {}

   public:
    WalResult<WalRecord> next();

    static WalResult<WalReader> create(io::IOEngine& engine,
                                       const std::string& path);
};

}  // namespace enigmadb::storage::wal

#endif  // ENIGMA_DB_WAL_READER_H
