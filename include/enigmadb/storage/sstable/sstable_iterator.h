/**
 * @file sstable_iterator.h
 * @brief Forward-only iterator over entries in a single SSTable file.
 *
 * Implements the Iterator interface by reading data blocks on demand
 * from an open SSTable via its index entries. Blocks are loaded lazily —
 * only the current block is held in memory at any time.
 *
 * @see Iterator for the general cursor contract and usage pattern.
 * @see SSTableReader for point lookups (when a full scan is not needed).
 *
 * @author frostzt
 * @date 2026-06-03
 */

#ifndef ENIGMA_DB_SSTABLE_ITERATOR_H
#define ENIGMA_DB_SSTABLE_ITERATOR_H

#include <optional>
#include <vector>

#include "enigmadb/common/error.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/iterator.h"
#include "enigmadb/storage/memtable/memtable.h"
#include "enigmadb/storage/sstable/sstable_common.h"

namespace enigmadb::storage::sstable {

/**
 * @brief Concrete Iterator that sequentially scans every entry in an
 *        SSTable, block by block.
 *
 * The iterator does not own the file handle or the index — both are
 * borrowed from an SSTableReader and must outlive this object. Blocks
 * are read into an internal buffer that is reused across block
 * transitions.
 *
 * When an I/O or decoding error occurs, the iterator becomes invalid
 * (valid() returns false). Call status() after the scan loop to
 * retrieve the error.
 *
 * @note The iterator currently reads blocks in index order (first to
 *       last) and does not support seeking to an arbitrary key.
 */
class SSTableIterator : public Iterator {
   private:
    io::IOEngine& engine_;
    const io::FileHandle& fh_;
    const std::vector<IndexEntry>& index_entries_;
    std::optional<common::Error> error_;

    size_t current_block_idx_;
    std::vector<uint8_t> block_buffer_;
    size_t block_offset_;

    std::vector<uint8_t> current_key_;
    memtable::MemtableValue current_value_;
    bool valid_;

    /**
     * @brief Loads the data block at current_block_idx_ into block_buffer_.
     *
     * @return True on success, false on I/O error (error_ is set).
     */
    bool load_block();

    /**
     * @brief Records an error and invalidates the iterator.
     *
     * @param err  The error to store for later retrieval via status().
     */
    void set_error(common::Error err);

   public:
    SSTableIterator(io::IOEngine& engine, const io::FileHandle& fh,
                    const std::vector<IndexEntry>& index_entries)
        : engine_(engine),
          fh_(fh),
          index_entries_(index_entries),
          error_(std::nullopt),
          current_block_idx_(0),
          block_offset_(0),
          valid_(false) {}

    bool valid() const override;
    void seek_to_first() override;
    void next() override;
    common::ExpectResult<void, common::Error> status() const override;
    const std::vector<uint8_t>& key() const override;
    const memtable::MemtableValue& value() const override;
};

};  // namespace enigmadb::storage::sstable

#endif  // ENIGMA_DB_SSTABLE_ITERATOR_H
