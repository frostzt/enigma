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

#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/dazzle_db/internal_iterator.h"
#include "enigmadb/storage/dazzle_db/internal_value.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"
#include "enigmadb/storage/key.h"

namespace enigmadb::dazzle {

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
class SSTableIterator : public InternalIterator {
   private:
    io::IOEngine& engine_;
    const io::FileHandle& fh_;
    const std::vector<IndexEntry>& index_entries_;
    std::optional<Error> error_;

    size_t current_block_idx_;
    std::vector<uint8_t> block_buffer_;
    size_t block_offset_;

    storage::Key current_key_;
    InternalValue current_value_;
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
    void set_error(Error err);

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
    Result<void> status() const override;
    const storage::Key& key() const override;
    const InternalValue& value() const override;
};

};  // namespace enigmadb::dazzle

#endif  // ENIGMA_DB_SSTABLE_ITERATOR_H
