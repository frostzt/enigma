/**
 * @file sstable_writer.h
 * @brief Sequential writer for SSTable (Sorted String Table) files.
 *
 * Produces an SSTable with the following on-disk layout:
 *
 * @code
 * ┌──────────────────────────────────────────┐
 * │  Data Block 0                            │
 * │  ┌──────────────────────────────────────┐ │
 * │  │ key_len (4B) | key | val_len (4B)   │ │
 * │  │ | value | is_tombstone (1B)          │ │
 * │  │ ... repeated per entry ...           │ │
 * │  └──────────────────────────────────────┘ │
 * │  Data Block 1                            │
 * │  ...                                     │
 * ├──────────────────────────────────────────┤
 * │  Index Block                             │
 * │  ┌──────────────────────────────────────┐ │
 * │  │ key_len (4B) | first_key             │ │
 * │  │ block_offset (8B) | block_size (4B)  │ │
 * │  │ ... repeated per data block ...      │ │
 * │  └──────────────────────────────────────┘ │
 * ├──────────────────────────────────────────┤
 * │  Footer (32B fixed)                      │
 * │  index_block_offset (8B)                 │
 * │  index_block_size   (4B)                 │
 * │  entry_count        (4B)                 │
 * │  format_version     (2B)                 │
 * │  checksum           (4B)  ← crc-32 of prev 18 bytes
 * │  magic "ENIGSSTB"   (8B)                 │
 * │  padding            (2B)                 │
 * └──────────────────────────────────────────┘
 * @endcode
 *
 * All multi-byte integers are big-endian. Data blocks are flushed to
 * disk when they exceed MAX_PAGING_SIZE_BYTES. Entries within each
 * block are stored in insertion order (which must be sorted by the
 * caller, typically from a memtable flush).
 *
 * Typical usage:
 * @code
 * auto writer = SSTableWriter::create(engine, "/data/000001.sst");
 * for (auto& [key, val] : memtable)
 *     writer->add(key, val);
 * writer->finish();  // flushes remaining data, writes index + footer, syncs
 * @endcode
 *
 * @author frostzt
 * @date 2026-04-08
 */

#ifndef ENIGMA_DB_SSTABLE_WRITER_H
#define ENIGMA_DB_SSTABLE_WRITER_H

#include <string>
#include <utility>
#include <vector>

#include "enigmadb/common/bloom_filter.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/memtable/memtable.h"
#include "enigmadb/storage/sstable/sstable_common.h"

namespace enigmadb::storage::sstable {

/**
 * @brief Writes a single SSTable file from a stream of sorted key-value pairs.
 *
 * SSTableWriter buffers entries into fixed-size data blocks, flushing each
 * block to disk when it exceeds MAX_PAGING_SIZE_BYTES. Once all entries
 * have been added, finish() writes the index block, footer, and syncs
 * the file and its parent directory to stable storage.
 *
 * SSTableWriter is move-only and holds a non-owning reference to the
 * IOEngine. Obtain instances exclusively through SSTableWriter::create().
 *
 * @note Entries must be added in sorted key order (as produced by a
 *       memtable iteration). The writer does not re-sort.
 */
class SSTableWriter {
   private:
    io::IOEngine& engine_;
    io::FileHandle fh_;
    std::string path_;

    std::vector<uint8_t> buffer_;  ///< Current data block being built.
    std::vector<uint8_t>
        current_block_first_key_;        ///< First key of the current block.
    size_t current_file_offset_;         ///< Bytes written to disk so far.
    size_t current_block_start_offset_;  ///< File offset where the current
                                         ///< block starts.
    common::BloomFilter bloom_filter_;

    std::vector<IndexEntry>
        index_entries_;   ///< Accumulated index for all flushed blocks.
    size_t entry_count_;  ///< Total entries added across all blocks.

    /**
     * @brief Private constructor; use SSTableWriter::create() instead.
     *
     * @param engine  IOEngine used for all I/O (non-owning reference).
     * @param path    Filesystem path of the SSTable being written.
     * @param fh      Open file handle (ownership is moved in).
     */
    SSTableWriter(io::IOEngine& engine, const std::string& path,
                  io::FileHandle fh, size_t estimated_keys)
        : engine_(engine),
          fh_(std::move(fh)),
          path_(path),
          current_file_offset_(0),
          current_block_start_offset_(0),
          bloom_filter_(estimated_keys, 0.01),
          entry_count_(0) {
        buffer_.reserve(MAX_PAGING_SIZE_BYTES);
    }

    /**
     * @brief Flushes the current data block to disk and records an
     *        index entry for it.
     *
     * After flushing, the internal buffer is cleared (but its allocation
     * is retained) and current_block_first_key_ is reset.
     *
     * @return Success, or an error if the write fails.
     */
    SSTExpectResult<void> flush_block();

   public:
    /**
     * @brief Factory that opens (or creates) an SSTable file and returns
     *        a ready-to-use writer.
     *
     * The file is opened in write mode via @p engine.
     *
     * @param engine         IOEngine to use for all I/O on this file.
     * @param path           Filesystem path for the SSTable file.
     * @param estimated_keys Pre-emptive approximate amount of keys in this
     * SSTable.
     * @return An SSTableWriter on success, or an error if the file
     *         cannot be opened.
     */
    static SSTExpectResult<SSTableWriter> create(io::IOEngine& engine,
                                                 const std::string& path,
                                                 size_t estimated_keys);

    /**
     * @brief Appends a key-value entry to the SSTable.
     *
     * If the current data block would exceed MAX_PAGING_SIZE_BYTES after
     * adding this entry, the block is flushed to disk first and a new
     * block is started.
     *
     * @param[in] key    Encoded composite key (must be in sorted order
     *                   relative to all previously added keys).
     * @param[in] value  Memtable value; tombstones are preserved as-is.
     * @return Success, or an error if a block flush fails.
     */
    SSTExpectResult<void> add(const std::vector<uint8_t>& key,
                              const memtable::MemtableValue& value);

    /**
     * @brief Finalizes the SSTable file.
     *
     * Performs the following steps in order:
     *   1. Flushes any remaining buffered data as a final block.
     *   2. Writes the index block (one entry per data block).
     *   3. Writes the 32-byte footer (index offset, entry count,
     *      format version, CRC-32 checksum over the first 18 footer
     *      bytes, magic bytes, and 2 bytes of padding).
     *   4. Calls sync_all() to flush file data and metadata to
     *      stable storage.
     *   5. Syncs the parent directory to ensure the file entry
     *      itself is durable.
     *
     * After finish() returns successfully the SSTable is complete
     * and crash-safe. The writer should not be used after this call.
     *
     * @return Success, or an error if any write or sync fails.
     */
    SSTExpectResult<void> finish();
};

}  // namespace enigmadb::storage::sstable

#endif  // ENIGMA_DB_SSTABLE_WRITER_H
