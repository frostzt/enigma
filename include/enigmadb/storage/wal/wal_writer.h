/**
 * @file wal_writer.h
 * @brief WAL Writer
 *
 * @author frostzt
 * @date 2026-04-03
 */

#ifndef ENIGMA_DB_WAL_WRITER_H
#define ENIGMA_DB_WAL_WRITER_H

#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/wal/wal_record.h"

namespace enigmadb::storage::wal {

class WalWriter {
   private:
    std::string path_;
    enigmadb::io::FileHandle fh_;
    enigmadb::io::IOEngine& engine_;

    WalWriter(const std::string& path, enigmadb::io::FileHandle fh,
              enigmadb::io::IOEngine& engine)
        : path_(path), fh_(std::move(fh)), engine_(engine) {}

   public:
    WalResult<void> append(const WalRecord& record);

    WalResult<void> sync();

    static WalResult<WalWriter> create(io::IOEngine& engine,
                                       const std::string& path);
};
}  // namespace enigmadb::storage::wal

#endif  // ENIGMA_DB_WAL_WRITER_H
