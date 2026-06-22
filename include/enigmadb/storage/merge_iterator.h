/**
 * @file merge_iterator.h
 * @brief K-way merge iterator that combines multiple sorted sources
 *        into a single deduplicated, sorted stream.
 *
 * MergeIterator is the read-path component that unifies the memtable
 * iterator and one or more SSTable iterators into a single cursor.
 * It uses a min-heap to efficiently select the smallest key across
 * all sources at each step, and deduplicates entries sharing the
 * same composite key by keeping only the newest version (highest
 * sequence number).
 *
 * @see Iterator for the general cursor contract and usage pattern.
 * @see SSTableIterator, MemtableIterator for concrete sources.
 *
 * @author frostzt
 * @date 2026-06-03
 */

#ifndef ENIGMA_DB_MERGE_ITERATOR_H
#define ENIGMA_DB_MERGE_ITERATOR_H

#include <optional>
#include <queue>
#include <vector>

#include "enigmadb/common/error.h"
#include "enigmadb/storage/iterator.h"
#include "enigmadb/storage/key_encoding.h"
#include "enigmadb/storage/memtable/memtable.h"

namespace enigmadb::storage {

/**
 * @brief A heap node that wraps a pointer to a source iterator.
 *
 * The heap never copies entries — it holds a borrowed pointer to
 * the source whose current position supplies the key and value.
 */
struct HeapEntry {
    Iterator* source_;  ///< Non-owning pointer to the source iterator.
};

/**
 * @brief Min-heap comparator for HeapEntry, ordered by composite key
 *        then by sequence number (descending) as a tiebreaker.
 *
 * For the std::priority_queue (which is a max-heap by default),
 * returning true means "a has lower priority than b." So:
 *   - If a's key < b's key → a has higher priority → return false.
 *   - If keys are equal → higher sequence number wins → the entry
 *     with the lower sequence number is deprioritized.
 *
 * This ensures that when duplicate keys exist across sources, the
 * most recent write (highest sequence) surfaces at the top of the
 * heap first.
 */
struct HeapCompare {
    CompositeKeyComparator key_cmp;
    bool operator()(const HeapEntry& a, const HeapEntry& b) const {
        const auto& akey = a.source_->key();
        const auto& bkey = b.source_->key();

        // if a's key < b's key  → a is HIGHER priority → return false
        if (key_cmp(akey, bkey)) return false;

        // if b's key < a's key  → a is LOWER  priority → return true
        if (key_cmp(bkey, akey)) return true;

        // keys equal → newer (higher seq) wins the top → a lower if older
        if (a.source_->value().sequence < b.source_->value().sequence) {
            return true;
        }

        return false;
    }
};

/**
 * @brief K-way merge iterator that produces a single sorted,
 *        deduplicated stream from multiple source iterators.
 *
 * All sources must individually produce entries in sorted composite
 * key order. The merge iterator maintains a min-heap of source
 * iterators and, at each step:
 *   1. Pops the source with the smallest key (and highest sequence
 *      number among ties).
 *   2. Copies its key and value as the current entry.
 *   3. Drains and advances all other sources that share the same
 *      key (deduplication).
 *
 * This means the caller sees each unique composite key exactly once,
 * with the value from the most recent write — which may be a
 * tombstone.
 *
 * MergeIterator does not own the source iterators; the caller must
 * ensure they outlive this object.
 *
 * @note If any source iterator reports an error during traversal,
 *       the merge iterator becomes invalid and the error is
 *       retrievable via status().
 */
class MergeIterator : public Iterator {
   public:
    /**
     * @brief Constructs a merge iterator over the given sources.
     *
     * The iterator starts in an invalid state; call seek_to_first()
     * to begin iteration.
     *
     * @param sources  Non-owning pointers to source iterators. Each
     *                 source must produce entries in sorted composite
     *                 key order. Must outlive this MergeIterator.
     */
    explicit MergeIterator(std::vector<Iterator*> sources)
        : sources_(std::move(sources)), error_(std::nullopt), valid_(false) {}

    /// @copydoc Iterator::valid()
    bool valid() const override;

    /**
     * @brief Seeks all source iterators to their first entry and
     *        positions the merge iterator at the globally smallest key.
     *
     * Resets all internal state, calls seek_to_first() on every source,
     * pushes valid sources onto the heap, and advances to the first
     * winner. If any source reports an error during its seek, the
     * merge iterator becomes invalid immediately.
     */
    void seek_to_first() override;

    /**
     * @brief Advances to the next unique composite key in sorted order.
     *
     * Pops the current winner from the heap, deduplicates any other
     * sources sharing the same key, and positions at the next smallest
     * key. After this call, valid() may become false if all sources
     * are exhausted or an error occurred.
     */
    void next() override;

    /// @copydoc Iterator::key()
    const std::vector<uint8_t>& key() const override;

    /// @copydoc Iterator::value()
    const memtable::MemtableValue& value() const override;

    /// @copydoc Iterator::status()
    common::ExpectResult<void, common::Error> status() const override;

   private:
    std::vector<Iterator*> sources_;  ///< All source iterators (non-owning).

    /// @brief Min-heap of sources ordered by (key asc, sequence desc).
    std::priority_queue<HeapEntry, std::vector<HeapEntry>, HeapCompare> heap_;

    std::optional<common::Error>
        error_;  ///< Set on failure; nullopt while healthy.

    std::vector<uint8_t> current_key_;       ///< Key of the current winner.
    memtable::MemtableValue current_value_;  ///< Value of the current winner.
    bool valid_;  ///< True when positioned at a valid entry.

    /**
     * @brief Selects the heap winner, copies its key/value, and
     *        deduplicates all sources sharing the same key.
     *
     * The winner (smallest key, highest sequence) is popped and its
     * entry is copied into current_key_ / current_value_. Then any
     * remaining heap entries with an identical key are popped and
     * advanced (their older values are discarded).
     */
    void advance_to_winner();

    /**
     * @brief Advances a source iterator and re-pushes it onto the
     *        heap if it remains valid.
     *
     * If the source becomes invalid due to an error (as opposed to
     * exhaustion), the merge iterator is invalidated.
     *
     * @param src  Source iterator to advance.
     */
    void advance_and_repush(Iterator* src);

    /**
     * @brief Records an error and invalidates the iterator.
     *
     * @param err  The error to store for retrieval via status().
     */
    void set_error(common::Error err);

    /**
     * @brief Returns true if an error has been recorded.
     */
    bool is_error() const;
};

}  // namespace enigmadb::storage

#endif  // ENIGMA_DB_MERGE_ITERATOR_H
