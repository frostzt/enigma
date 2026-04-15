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

/* TODO: This could come from OS paging size too */
constexpr size_t MAX_PAGING_SIZE_BYTES = 4096;

template <typename T>
using SSTExpectResult = common::ExpectResult<T, common::Error>;

struct IndexEntry {
    std::vector<uint8_t> first_key;
    size_t block_offset;
    size_t block_size;
};

class SSTableWriter {
   private:
    io::IOEngine& engine_;
    io::FileHandle fh_;
    std::string path_;
    size_t max_block_bytes_;

    std::vector<uint8_t> buffer_;  // current block being built
    std::vector<uint8_t>
        current_block_first_key_;  // first key of the curr block
    size_t current_file_offset_;
    size_t current_block_start_offset_;
    std::vector<IndexEntry> index_entries_;
    size_t entry_count_;

    SSTableWriter(io::IOEngine& engine, const std::string& path,
                  io::FileHandle fh, size_t max_bytes)
        : engine_(engine),
          fh_(std::move(fh)),
          path_(path),
          max_block_bytes_(max_bytes),
          current_file_offset_(0),
          current_block_start_offset_(0),
          entry_count_(0) {
        /* The buffer will always deal with configured paging size */
        buffer_.reserve(MAX_PAGING_SIZE_BYTES);
    }

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
