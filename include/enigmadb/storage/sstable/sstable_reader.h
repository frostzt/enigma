/**
 * @file sstable_reader.h
 * @brief Read-only accessor for SSTable files produced by SSTableWriter.
 *
 * Opens an SSTable, validates its footer (magic bytes and CRC-32
 * checksum), loads the index block into memory, and provides point
 * lookups by composite key. The on-disk format consumed here is
 * documented in sstable_writer.h.
 *
 * @see SSTableWriter for the file layout and write path.
 *
 * @author frostzt
 * @date 2026-04-24
 */

#ifndef ENIGMA_DB_SSTABLE_READER_H
#define ENIGMA_DB_SSTABLE_READER_H

#include <vector>

#include "enigmadb/common/bloom_filter.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/memtable/memtable.h"
#include "enigmadb/storage/sstable/sstable_common.h"
#include "enigmadb/storage/sstable/sstable_iterator.h"

namespace enigmadb::storage::sstable {

/**
 * @brief Read-only handle to a single SSTable file.
 *
 * On creation the reader validates the footer, loads the full index
 * block into memory, and holds the file open for subsequent reads.
 * Data blocks are read on demand — only the block that may contain
 * the target key is loaded during a get() call.
 *
 * SSTableReader is move-only and holds a non-owning reference to the
 * IOEngine. Obtain instances exclusively through SSTableReader::create().
 */
class SSTableReader {
   private:
    io::IOEngine& engine_;
    io::FileHandle fh_;
    std::string path_;
    std::vector<IndexEntry>
        index_entries_;  ///< In-memory copy of the index block.
    common::BloomFilter bloom_filter_;
    MinimalSSTableFooter footer_;

    /**
     * @brief Private constructor; use SSTableReader::create() instead.
     *
     * @param engine       IOEngine used for all I/O (non-owning reference).
     * @param fh           Open file handle (ownership is moved in).
     * @param path         Filesystem path of the SSTable.
     * @param idx_entries  Decoded index entries (ownership is moved in).
     */
    SSTableReader(io::IOEngine& engine, io::FileHandle fh,
                  const std::string& path, std::vector<IndexEntry> idx_entries,
                  common::BloomFilter blf, MinimalSSTableFooter footer_)
        : engine_(engine),
          fh_(std::move(fh)),
          path_(path),
          index_entries_(std::move(idx_entries)),
          bloom_filter_(std::move(blf)),
          footer_(std::move(footer_)) {}

   public:
    /**
     * @brief Opens an SSTable file and returns a ready-to-use reader.
     *
     * Performs the following validation during construction:
     *   1. Verifies the file is at least 32 bytes (minimum footer size).
     *   2. Reads the footer and checks the magic bytes ("ENIGSSTB").
     *   3. Verifies the CRC-32 checksum over the first 18 footer bytes.
     *   4. Reads and decodes the full index block into memory.
     *
     * @param engine  IOEngine to use for all I/O on this file.
     * @param path    Filesystem path of an existing SSTable file.
     * @return An SSTableReader on success, or an error if the file
     *         cannot be opened, is too small, has invalid magic, a
     *         checksum mismatch, or a malformed index block.
     */
    static SSTExpectResult<SSTableReader> create(io::IOEngine& engine,
                                                 const std::string& path);

    /**
     * @brief Point lookup for a single composite key.
     *
     * Uses a binary search over the in-memory index to locate the
     * candidate data block, then performs a linear scan within that
     * block. Keys within the block are compared using
     * CompositeKeyComparator; the scan short-circuits once a key
     * greater than the target is encountered.
     *
     * @param[in] key  Encoded composite key (as produced by
     *                 encode_composite_key()).
     * @return The matching MemtableValue (which may be a tombstone) wrapped
     *         in std::optional, or std::nullopt if the key is not found.
     *         Returns an error if the data block read fails or the block
     *         is malformed.
     */
    SSTExpectResult<std::optional<memtable::MemtableValue>> get(
        const std::vector<uint8_t>& key);

    SSTExpectResult<MinimalSSTableFooter> get_footer() const {
        return footer_;
    };

    SSTableIterator iterator() const {
        return SSTableIterator(engine_, fh_, index_entries_);
    }
};

}  // namespace enigmadb::storage::sstable

#endif  // ENIGMA_DB_SSTABLE_READER_H
