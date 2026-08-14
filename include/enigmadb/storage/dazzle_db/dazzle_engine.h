/**
 * @file storage_engine.h
 * @brief Dazzle is an LSM Engine
 *
 * @author frostzt
 * @date 2026-04-05
 */

#ifndef ENIGMA_DB_DAZZLE_H
#define ENIGMA_DB_DAZZLE_H

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "enigmadb/base.h"
#include "enigmadb/hlc.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/dazzle_db/compaction/compaction.h"
#include "enigmadb/storage/dazzle_db/compaction/compaction_policy.h"
#include "enigmadb/storage/dazzle_db/core/version.h"
#include "enigmadb/storage/dazzle_db/core/version_set.h"
#include "enigmadb/storage/dazzle_db/memtable/memtable.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_reader.h"
#include "enigmadb/storage/dazzle_db/wal/wal_writer.h"
#include "enigmadb/storage/key.h"
#include "enigmadb/storage/storage_engine.h"
#include "enigmadb/storage/value.h"

namespace enigmadb::dazzle {

/**
 * @brief Core storage engine implementing an LSM-tree write path with
 *        WAL durability and SSTable persistence.
 *
 * Dazzle is move-only and holds a non-owning reference to the
 * IOEngine. Obtain instances exclusively through Dazzle::open().
 */
class Dazzle : public storage::StorageEngine {
   private:
    io::IOEngine& engine_;
    const std::string data_dir_;

    /// Active WAL segment writer.
    std::optional<WalWriter> wal_writer_;
    /// Flush threshold in bytes.
    uint64_t memtable_size_;
    /// Current mutable memtable.
    Memtable active_memtable_;
    /// Hybrid logical clock for record timestamps.
    TimestampGenerator hlc_;

    /* --------------------------------------------------
     * Version management
     * --------------------------------------------------*/
    std::unique_ptr<VersionSet> version_set_;

    Result<void> install_flushed_sst(SSTableId new_id, std::shared_ptr<SSTableReader> reader,
                                     std::shared_ptr<SSTableMeta> meta);

    /* --------------------------------------------------
     * Sequences
     * --------------------------------------------------*/
    /// Monotonically increasing log sequence number.
    std::atomic<uint64_t> lsn_{0};
    /// Sequence number for the next WAL segment.
    std::atomic<uint64_t> next_wal_seq_{0};
    /// Sequence number for the next SSTable file.
    std::atomic<uint64_t> next_sst_seq_{0};

    uint64_t bump_lsn_sequence() { return lsn_.fetch_add(1, std::memory_order_relaxed); }
    uint64_t mint_sst_id() { return next_sst_seq_.fetch_add(1, std::memory_order_relaxed); }
    uint64_t peek_sst_id() const { return next_sst_seq_.load(std::memory_order_relaxed); }
    uint64_t mint_wal_id() { return next_wal_seq_.fetch_add(1, std::memory_order_relaxed); }
    uint64_t peek_wal_id() const { return next_wal_seq_.load(std::memory_order_relaxed); }

    /* --------------------------------------------------
     * Compaction
     * --------------------------------------------------*/
    /// Compaction policy picker
    std::unique_ptr<CompactionPolicy> policy_;

    /// Compactor compacts SSTable files
    Compactor compactor_;

    /// Executes the compaction task
    Result<SSTableId> execute(const CompactionTask&);

    /// Registers the new SSTable reader and cleans up the deleted sstables
    Result<void> install(const CompactionTask&);

    /// Executes the compaction task and performs sst swap and cleanup
    Result<std::optional<SSTableId>> run_task(const CompactionTask&);

    std::vector<const SSTableMeta*> sst_meta_to_vector() const;

    /**
     * @brief Private constructor; use Dazzle::open() instead.
     */
    Dazzle(io::IOEngine& engine, std::string data_dir, WalWriter wal_writer, uint64_t memtable_size,
           Memtable active_memtable,
           std::map<SSTableId, std::shared_ptr<SSTableReader>, SSTableIdComparator> sst_readers,
           std::map<SSTableId, std::shared_ptr<SSTableMeta>, SSTableIdComparator> sst_meta, uint64_t next_wal_seq,
           uint64_t next_sst_seq, uint64_t highest_sequence = 0, std::unique_ptr<CompactionPolicy> policy = nullptr)
        : engine_(engine),
          data_dir_(data_dir),
          wal_writer_(std::move(wal_writer)),
          memtable_size_(memtable_size),
          active_memtable_(std::move(active_memtable)),
          lsn_{highest_sequence},
          next_wal_seq_{next_wal_seq},
          next_sst_seq_{next_sst_seq},
          policy_(policy ? std::move(policy) : std::make_unique<SizeTieredCompactionPolicy>(4, 8)),
          compactor_(Compactor::create(engine, data_dir)) {
        /* setup base version */
        auto boot_version = std::make_shared<Version>();
        boot_version->sst_readers = std::move(sst_readers);
        boot_version->sst_meta = std::move(sst_meta);

        /* create a version set out of the above */
        version_set_ = std::make_unique<VersionSet>();
        version_set_->append_version(std::move(boot_version));
    }

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
     */
    Result<void> put(const storage::Key& key, std::optional<std::span<const uint8_t>> value, bool remove);

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
    Dazzle(const Dazzle&) = delete;
    Dazzle& operator=(const Dazzle&) = delete;
    Dazzle(Dazzle&&) = delete;
    Dazzle& operator=(Dazzle&&) = delete;

    /* --------------------------------------------------
     * Compaction
     * --------------------------------------------------*/
    /* @brief Performs compaction based on what policy is picked */
    Result<std::optional<SSTableId>> do_compact_work();

    /* @brief Forces a full compaction
     *
     * WARN: Forces a FULL COMPACTION every tombstone will be dropped and just
     *       one file would be left
     */
    Result<std::optional<SSTableId>> compact_now();

    /**
     * @brief Overrides the current compaction policy and sets it as active
     */
    Result<void> set_compaction_policy(std::unique_ptr<CompactionPolicy> policy);

    /**
     * @brief Returns the latest LSN available
     */
    uint64_t latest_lsn() const { return lsn_.load(std::memory_order_relaxed); }

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
     */
    static Result<std::unique_ptr<Dazzle>> open(io::IOEngine& engine, const std::string& data_dir,
                                                const uint64_t memtable_size,
                                                std::unique_ptr<CompactionPolicy> policy = nullptr);

    /**
     * @brief Writes a column value.
     *
     * The value is first durably written to the WAL, then applied to
     * the active memtable. If the memtable exceeds its size threshold
     * a flush is triggered automatically.
     */
    Result<void> put(const storage::Key& key, std::span<const uint8_t> value) override;

    /**
     * @brief Deletes a column by writing a tombstone.
     *
     * The delete is first durably written to the WAL, then applied
     * to the active memtable. The tombstone propagates to SSTables
     * on flush and suppresses older values during reads.
     */
    Result<void> remove(const storage::Key& key) override;

    /* TODO: Implementation pending */
    // Result<std::unique_ptr<storage::Iterator>> scan(
    //     const storage::KeyRange& range) override;

    /**
     * @brief Point lookup for a single column value.
     *
     * Checks the active memtable first, then scans SSTables from
     * newest to oldest. A tombstone at any level is treated as a
     * definitive delete — older SSTables are not consulted.
     */
    Result<std::optional<storage::Value>> get(const storage::Key&) override;

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

    /**
     * @NOTE: TEST ONLY
     */
    Result<std::optional<InternalValue>> get_internal(const storage::Key&);
};

}  // namespace enigmadb::dazzle

#endif  // ENIGMA_DB_DAZZLE_H
