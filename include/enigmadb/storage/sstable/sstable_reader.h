/**
 * @file sstable_reader.h
 *
 * @author frostzt
 * @date 2026-04-24
 */

#ifndef ENIGMA_DB_SSTABLE_READER_H
#define ENIGMA_DB_SSTABLE_READER_H

#include <vector>

#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/memtable/memtable.h"
#include "enigmadb/storage/sstable/sstable_common.h"

namespace enigmadb::storage::sstable {

class SSTableReader {
   private:
    io::IOEngine& engine_;
    io::FileHandle fh_;
    std::string path_;
    std::vector<IndexEntry> index_entries_;

    SSTableReader(io::IOEngine& engine, io::FileHandle fh,
                  const std::string& path, std::vector<IndexEntry> idx_entries)
        : engine_(engine),
          fh_(std::move(fh)),
          path_(path),
          index_entries_(std::move(idx_entries)) {}

   public:
    static SSTExpectResult<SSTableReader> create(io::IOEngine& engine,
                                                 const std::string& path);

    SSTExpectResult<std::optional<memtable::MemtableValue>> get(
        const std::vector<uint8_t>& key);
};

}  // namespace enigmadb::storage::sstable
#endif  // ENIGMA_DB_SSTABLE_READER_H
