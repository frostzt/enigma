/**
 * @file sstable_writer.h
 *
 * @author frostzt
 * @date 2026-04-08
 */

#ifndef ENIGMA_DB_SSTABLE_WRITER_H
#define ENIGMA_DB_SSTABLE_WRITER_H

#include <string>
#include <utility>
#include <vector>

#include "enigmadb/common/error.h"
#include "enigmadb/common/result.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/memtable/memtable.h"

namespace enigmadb::storage::sstable {

template <typename T>
using SSTExpectResult = common::ExpectResult<T, common::Error>;

class SSTableWriter {
   private:
    std::vector<uint8_t> buffer_;
    std::vector<uint8_t> current_block_first_key_;
    size_t current_offset_;
    size_t current_file_offset_;

    io::IOEngine& engine_;
    std::string path_;
    io::FileHandle fh_;
    size_t max_bytes_;

    SSTableWriter(io::IOEngine& engine, const std::string& path,
                  io::FileHandle fh, size_t max_bytes)
        :

          buffer_(max_bytes),
          current_offset_(0),
          current_file_offset_(0),
          engine_(engine),
          path_(path),
          fh_(std::move(fh)),
          max_bytes_(max_bytes) {}

    SSTExpectResult<std::vector<uint8_t>> serialize();

   public:
    static SSTExpectResult<SSTableWriter> create(io::IOEngine& engine,
                                                 const std::string& path,
                                                 const size_t max_bytes);

    SSTExpectResult<void> add(const std::vector<uint8_t>& key,
                              const memtable::MemtableValue& value);

    SSTExpectResult<void> flush_block();

    SSTExpectResult<void> finish();
};

}  // namespace enigmadb::storage::sstable

#endif  // ENIGMA_DB_SSTABLE_WRITER_H
