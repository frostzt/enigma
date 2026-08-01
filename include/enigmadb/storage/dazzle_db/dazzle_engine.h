/**
 * @file storage_engine.h
 * @brief Dazzle is an LSM Engine
 *
 * @author frostzt
 * @date 2026-04-05
 */

#ifndef ENIGMA_DB_STORAGE_ENGINE_H
#define ENIGMA_DB_STORAGE_ENGINE_H

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "enigmadb/hlc.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/dazzle_db/compaction/compaction.h"
#include "enigmadb/storage/dazzle_db/memtable/memtable.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_reader.h"
#include "enigmadb/storage/dazzle_db/wal/wal_writer.h"

namespace enigmadb::dazzle {

/**
 * @brief Core storage engine implementing an LSM-tree write path with
 *        WAL durability and SSTable persistence.
 *
 * Dazzle is move-only and holds a non-owning reference to the
 * IOEngine. Obtain instances exclusively through Dazzle::open().
 */
class Dazzle {
   private:
    io::IOEngine& engine_;
    const std::string data_dir_;

    /// SSTable readers
    std::map<SSTableId, std::unique_ptr<SSTableReader>, SSTableIdComparator>
        sst_readers_;
    /// Active WAL segment writer.
    std::optional<WalWriter> wal_writer_;
    /// Flush threshold in bytes.
    uint64_t memtable_size_;
    /// Current mutable memtable.
    Memtable active_memtable_;
    /// Hybrid logical clock for record timestamps.
    TimestampGenerator hlc_;

    /* --------------------------------------------------
     * Sequences
     * --------------------------------------------------*/
    /// Monotonically increasing log sequence number.
    std::atomic<uint64_t> lsn_{0};
    /// Sequence number for the next WAL segment.
    std::atomic<uint64_t> next_wal_seq_{0};
    /// Sequence number for the next SSTable file.
    std::atomic<uint64_t> next_sst_seq_{0};

    /**
     * @brief Bumps LSN sequence by one
     */
    uint64_t bump_lsn_sequence() {
        return lsn_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Bumps SST file sequence by one
     */
    uint64_t bump_sst_sequence() {
        return next_sst_seq_.fetch_add(1, std::memory_order_relaxed);
    };

    /**
     * @brief Bumps WAL file sequence by one
     */
    uint64_t bump_wal_sequence() {
        return next_wal_seq_.fetch_add(1, std::memory_order_relaxed);
    };

    /* --------------------------------------------------
     * Compaction
     * --------------------------------------------------*/
    /// Configuration for compaction
    CompactionConfig compaction_config_;
    /// Compactor compacts SSTable files
    Compactor compactor_;

    /**
     * @brief Private constructor; use Dazzle::open() instead.
     */
    Dazzle(
        io::IOEngine& engine, std::string data_dir, WalWriter wal_writer,
        uint64_t memtable_size, Memtable active_memtable,
        std::map<SSTableId, std::unique_ptr<SSTableReader>, SSTableIdComparator>
            sst_readers,
        uint64_t next_wal_seq, uint64_t next_sst_seq,
        uint64_t highest_sequence = 0,
        CompactionConfig compaction_config = SizeTieredConfig{2, 6})
        : engine_(engine),
          data_dir_(data_dir),
          sst_readers_(std::move(sst_readers)),
          wal_writer_(std::move(wal_writer)),
          memtable_size_(memtable_size),
          active_memtable_(std::move(active_memtable)),
          lsn_{highest_sequence},
          next_wal_seq_{next_wal_seq},
          next_sst_seq_{next_sst_seq},
          compaction_config_(std::move(compaction_config)),
          compactor_(Compactor::create(engine, data_dir)) {}

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
    Result<void> put(const std::vector<uint8_t>& partition_key,
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

    /**
     * @brief Given the current configuration checks weather sstables
     *        should be compacted or not.
     */
    bool should_compact() const;

    /**
     * @brief Returns weather we should run compaction for size tiered
     *        compaction strategy.
     */
    bool should_compact_size_tiered(const SizeTieredConfig& opts) const;

   public:
    Dazzle(const Dazzle&) = delete;
    Dazzle& operator=(const Dazzle&) = delete;
    Dazzle(Dazzle&&) = delete;
    Dazzle& operator=(Dazzle&&) = delete;

    /**
     * @brief Returns the latest LSN available
     */
    uint64_t latest_lsn() const { return lsn_.load(std::memory_order_relaxed); }

    /**
     * @brief Returns the next wal sequence number available
     */
    uint64_t get_next_wal_sequence() const {
        return next_wal_seq_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Returns the next sstable sequence number available
     */
    uint64_t get_next_sst_sequence() const {
        return next_sst_seq_.load(std::memory_order_relaxed);
    }

    Result<SSTableId> do_compact_work();

    /**
     * @brief Overrides the current compaction config and sets it to the
     *        config provided.
     */
    Result<void> set_compaction_config(CompactionConfig config);

    /**
     * @brief Returns the current directory where sstables files are stored
     */
    std::string get_sst_directory() const { return data_dir_ + "/sst"; };

    /**
     * @brief Returns the current directory where wal files are stored
     */
    std::string get_wal_directory() const { return data_dir_ + "/wal"; };

    /**
     * @brief Opens or bootstraps a Dazzle rooted at @p data_dir.
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
     * @return A ready-to-use Dazzle, or an error if directory
     *         creation, SSTable opening, WAL creation, or recovery fails.
     */
    static Result<std::unique_ptr<Dazzle>> open(io::IOEngine& engine,
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
    Result<std::optional<InternalValue>> get(
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
};

}  // namespace enigmadb::dazzle

#endif  // ENIGMA_DB_STORAGE_ENGINE_H
