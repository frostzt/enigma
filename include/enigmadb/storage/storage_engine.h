/**
 * @file storage_engine.h
 * @brief Top-level storage engine that ties together the WAL, memtable,
 *        and SSTable layers.
 *
 * StorageEngine is the single entry point for all reads and writes in
 * EnigmaDB. The write path follows a classic LSM-tree pattern:
 *
 *   1. Append to the write-ahead log (WAL) and sync.
 *   2. Apply the mutation to the in-memory memtable.
 *   3. When the memtable exceeds its size threshold, flush it to a
 *      new SSTable and rotate the WAL.
 *
 * The read path checks the memtable first, then scans SSTables from
 * newest to oldest, returning the first match (or honoring a tombstone
 * as a definitive delete).
 *
 * On startup, open() recovers any unflushed WAL segments by replaying
 * them into the memtable, flushing to an SSTable, and cleaning up the
 * old WAL files.
 *
 * Directory layout managed by the engine:
 * @code
 * <data_dir>/
 * ├── wal/
 * │   ├── wal_00000001.log
 * │   └── wal_00000002.log
 * └── sst/
 *     ├── sst_00000001.db
 *     └── sst_00000002.db
 * @endcode
 *
 * @author frostzt
 * @date 2026-04-05
 */

#ifndef ENIGMA_DB_STORAGE_ENGINE_H
#define ENIGMA_DB_STORAGE_ENGINE_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "enigmadb/common/hlc.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/memtable/memtable.h"
#include "enigmadb/storage/sstable/sstable_reader.h"
#include "enigmadb/storage/wal/wal_writer.h"

namespace enigmadb::storage {

/// @brief Convenience alias for result types in the storage layer.
template <typename T>
using Result = common::ExpectResult<T, common::Error>;

/**
 * @brief Core storage engine implementing an LSM-tree write path with
 *        WAL durability and SSTable persistence.
 *
 * StorageEngine is move-only and holds a non-owning reference to the
 * IOEngine. Obtain instances exclusively through StorageEngine::open().
 */
class StorageEngine {
   private:
    io::IOEngine& engine_;
    const std::string data_dir_;

    std::optional<wal::WalWriter> wal_writer_;  ///< Active WAL segment writer.
    uint64_t memtable_size_;                    ///< Flush threshold in bytes.
    memtable::Memtable active_memtable_;        ///< Current mutable memtable.
    std::vector<sstable::SSTableReader>
        sst_readers_;  ///< SSTable readers, oldest first.
    common::TimestampGenerator
        hlc_;                ///< Hybrid logical clock for record timestamps.
    uint64_t lsn_;           ///< Monotonically increasing log sequence number.
    uint64_t next_wal_seq_;  ///< Sequence number for the next WAL segment.
    uint64_t next_sst_seq_;  ///< Sequence number for the next SSTable file.

    /**
     * @brief Private constructor; use StorageEngine::open() instead.
     */
    StorageEngine(io::IOEngine& engine, std::string data_dir,
                  wal::WalWriter wal_writer, uint64_t memtable_size,
                  memtable::Memtable active_memtable,
                  std::vector<sstable::SSTableReader> sst_readers,
                  uint64_t next_wal_seq, uint64_t next_sst_seq,
                  uint64_t highest_sequence = 0)
        : engine_(engine),
          data_dir_(std::move(data_dir)),
          wal_writer_(std::move(wal_writer)),
          memtable_size_(memtable_size),
          active_memtable_(std::move(active_memtable)),
          sst_readers_(std::move(sst_readers)),
          lsn_(highest_sequence),
          next_wal_seq_(next_wal_seq),
          next_sst_seq_(next_sst_seq) {}

    /**
     * @brief Returns the filesystem path for a WAL segment with the
     *        given sequence number.
     *
     * @param seq  WAL sequence number (zero-padded to 8 digits in the
     * filename).
     * @return Path of the form `<data_dir>/wal/wal_00000001.log`.
     */
    std::string wal_path(uint64_t seq);

    /**
     * @brief Returns the filesystem path for an SSTable with the
     *        given sequence number.
     *
     * @param seq  SSTable sequence number (zero-padded to 8 digits in the
     * filename).
     * @return Path of the form `<data_dir>/sst/sst_00000001.db`.
     */
    std::string sst_path(uint64_t seq);

    /**
     * @brief Internal write path shared by put() and remove().
     *
     * Constructs a WalRecord, appends and syncs it to the active WAL,
     * applies the mutation to the memtable, and triggers a flush if
     * the memtable has exceeded its size threshold.
     *
     * @param[in] partition_key   Raw bytes of the partition key.
     * @param[in] clustering_key  Raw bytes of the clustering key.
     * @param[in] column_name     Column being written or deleted.
     * @param[in] value           Column value; std::nullopt for deletes.
     * @param     remove          True if this is a delete (tombstone)
     * operation.
     * @return Success, or an error if the WAL write, sync, or flush fails.
     */
    Result<void> put_record(const std::vector<uint8_t>& partition_key,
                            const std::vector<uint8_t>& clustering_key,
                            const std::string& column_name,
                            const std::optional<std::vector<uint8_t>>& value,
                            bool remove);

    /**
     * @brief Replays old WAL segments into the memtable and flushes
     *        to an SSTable.
     *
     * Called during open(). Scans the WAL directory for segments with
     * sequence numbers below the current active WAL, replays their
     * records into the active memtable, flushes to a new SSTable,
     * and deletes the old WAL files.
     *
     * @return Success, or an error if a WAL cannot be read or the
     *         flush fails.
     */
    Result<uint64_t> recover();

   public:
    /**
     * @brief Opens or bootstraps a StorageEngine rooted at @p data_dir.
     *
     * Creates the `wal/` and `sst/` subdirectories if they don't exist,
     * opens all existing SSTable files as readers (sorted by sequence
     * number), opens a new WAL segment for writes, and runs crash
     * recovery by replaying any old WAL segments.
     *
     * @param engine          IOEngine to use for all file I/O.
     * @param data_dir        Root directory for all storage files.
     * @param memtable_size   Approximate byte threshold at which the
     *                        memtable is flushed to an SSTable.
     * @return A ready-to-use StorageEngine, or an error if directory
     *         creation, SSTable opening, WAL creation, or recovery fails.
     */
    static Result<StorageEngine> open(io::IOEngine& engine,
                                      const std::string& data_dir,
                                      const uint64_t memtable_size);

    /**
     * @brief Writes a column value.
     *
     * The value is first durably written to the WAL, then applied to
     * the active memtable. If the memtable exceeds its size threshold
     * a flush is triggered automatically.
     *
     * @param[in] partition_key   Raw bytes of the partition key.
     * @param[in] clustering_key  Raw bytes of the clustering key.
     * @param[in] column_name     Column being written.
     * @param[in] value           Raw column value; must not be empty.
     * @return Success, or an error if the value is empty or any I/O fails.
     */
    Result<void> put(const std::vector<uint8_t>& partition_key,
                     const std::vector<uint8_t>& clustering_key,
                     const std::string& column_name,
                     const std::vector<uint8_t>& value);

    /**
     * @brief Deletes a column by writing a tombstone.
     *
     * The delete is first durably written to the WAL, then applied
     * to the active memtable. The tombstone propagates to SSTables
     * on flush and suppresses older values during reads.
     *
     * @param[in] partition_key   Raw bytes of the partition key.
     * @param[in] clustering_key  Raw bytes of the clustering key.
     * @param[in] column_name     Column to delete.
     * @return Success, or an error if any I/O fails.
     */
    Result<void> remove(const std::vector<uint8_t>& partition_key,
                        const std::vector<uint8_t>& clustering_key,
                        const std::string& column_name);

    /**
     * @brief Point lookup for a single column value.
     *
     * Checks the active memtable first, then scans SSTables from
     * newest to oldest. A tombstone at any level is treated as a
     * definitive delete — older SSTables are not consulted.
     *
     * @param[in] partition_key   Raw bytes of the partition key.
     * @param[in] clustering_key  Raw bytes of the clustering key.
     * @param[in] column_name     Column to look up.
     * @return The column value wrapped in std::optional, or std::nullopt
     *         if the key is absent or tombstoned. Returns an error if
     *         any SSTable read fails.
     */
    Result<std::optional<memtable::MemtableValue>> get(
        const std::vector<uint8_t>& partition_key,
        const std::vector<uint8_t>& clustering_key,
        const std::string& column_name);

    /**
     * @brief Flushes the active memtable to a new SSTable and rotates
     *        the WAL.
     *
     * Performs the following steps:
     *   1. Writes all memtable entries to a new SSTable via SSTableWriter.
     *   2. Opens an SSTableReader for the newly created file.
     *   3. Creates a new WAL segment for subsequent writes.
     *   4. Replaces the active memtable with an empty one.
     *   5. Best-effort deletes the old WAL segment (failure is non-fatal).
     *
     * No-op if the memtable is empty.
     *
     * @return Success, or an error if the SSTable write, WAL creation,
     *         or sync fails.
     */
    Result<void> flush();

    uint64_t latest_lsn() const { return lsn_; };
};

}  // namespace enigmadb::storage

#endif  // ENIGMA_DB_STORAGE_ENGINE_H
