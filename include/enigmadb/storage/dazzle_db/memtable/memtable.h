/**
 * @file memtable.h
 * @brief In-memory sorted write buffer for the EnigmaDB storage layer.
 *
 * Memtable is the first landing point for all writes (puts and deletes).
 * Entries are keyed by (partition_key, clustering_key, column_name)
 * composite keys and maintained in sorted order for efficient point
 * lookups and future range scans. Once the memtable exceeds a size
 * threshold it should be flushed to an on-disk SSTable.
 *
 * @todo The backing structure is currently std::map (red-black tree).
 *       Replace with a concurrent skip list to allow lock-free reads
 *       alongside writes and improve cache locality for sequential scans.
 *
 * @author frostzt
 * @date 2026-04-05
 */

#ifndef ENIGMA_DB_MEMTABLE_H
#define ENIGMA_DB_MEMTABLE_H

#include <stdint.h>

#include <cstdint>
#include <map>
#include <optional>
#include <span>

#include "enigmadb/storage/dazzle_db/internal_value.h"
#include "enigmadb/storage/key.h"

namespace enigmadb::dazzle {

/**
 * @brief In-memory sorted write buffer keyed by composite keys.
 *
 * All mutations (put / remove) are applied here first. Reads check the
 * memtable before consulting on-disk storage. The memtable tracks its
 * approximate memory footprint and signals when it should be flushed.
 *
 * @see CompositeKeyComparator for the key ordering semantics.
 */
class Memtable {
   private:
    size_t bytes_;
    size_t max_memtable_size_;
    std::map<storage::Key, InternalValue> entries_;

   public:
    /**
     * @brief Constructs an empty memtable with the given flush threshold.
     *
     * @param flush_after  Approximate byte count at which should_flush()
     *                     begins returning true.
     */
    Memtable(size_t flush_after) : bytes_(0), max_memtable_size_(flush_after) {}

    /**
     * @brief Inserts or overwrites a column value.
     *
     * If a tombstone previously existed for this key it is replaced
     * with the live value.
     */
    void put(const storage::Key& key, std::span<const uint8_t> value,
             uint64_t sequence);

    /**
     * @brief Marks a column as deleted by writing a tombstone.
     *
     * The tombstone is retained in the memtable and eventually flushed
     * to disk so that older versions of this key are suppressed during
     * compaction.
     */
    void remove(const storage::Key& key, uint64_t sequence);

    /**
     * @brief Point lookup for a single column value.
     */
    std::optional<InternalValue> get(const storage::Key& key);

    /**
     * @brief Returns a rough estimate of the memory consumed by this memtable.
     *
     * The estimate includes key and value byte counts but may not
     * account for allocator overhead or node pointers in the backing
     * container.
     *
     * @return Approximate size in bytes.
     */
    size_t approximate_size() const;

    /**
     * @brief Indicates whether the memtable has exceeded its size threshold
     *        and should be flushed to disk.
     *
     * @return True if approximate_size() exceeds the configured flush limit.
     */
    bool should_flush() const;

    size_t count() const { return entries_.size(); };

    auto begin() const { return entries_.begin(); }

    auto end() const { return entries_.end(); }
};

}  // namespace enigmadb::dazzle

#endif  // ENIGMA_DB_MEMTABLE_H
