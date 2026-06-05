/**
 * @file iterator.h
 * @brief Abstract iterator interface for sequential traversal of
 *        sorted key-value storage.
 *
 * Provides a uniform cursor-style API over different backing stores
 * (memtables, SSTable data blocks, merged views). Concrete
 * implementations define how entries are fetched and ordered.
 *
 * Typical usage:
 * @code
 * iter->seek_to_first();
 * while (iter->valid()) {
 *     process(iter->key(), iter->value());
 *     iter->next();
 * }
 * if (auto s = iter->status(); !s.has_value()) {
 *     handle_error(s.err());
 * }
 * @endcode
 *
 * @author frostzt
 * @date 2026-06-03
 */

#ifndef ENIGMA_DB_ITERATOR_H
#define ENIGMA_DB_ITERATOR_H

#include <cstdint>
#include <vector>

#include "enigmadb/common/error.h"
#include "enigmadb/common/result.h"
#include "enigmadb/storage/memtable/memtable.h"

namespace enigmadb::storage {

/**
 * @brief Abstract forward-only cursor over a sorted sequence of
 *        key-value entries.
 *
 * An iterator is either *positioned* at a valid entry or
 * *invalidated* (before the first seek, after exhausting all entries,
 * or after an error). Callers must check valid() before accessing
 * key() or value(), and should inspect status() after iteration
 * completes to detect I/O or corruption errors that silently
 * invalidated the cursor.
 */
class Iterator {
   public:
    virtual ~Iterator() = default;

    /**
     * @brief Returns true if the iterator is positioned at a valid entry.
     *
     * When this returns false, key() and value() must not be called.
     * A false return may indicate either exhaustion or an error —
     * call status() to distinguish.
     */
    virtual bool valid() const = 0;

    /**
     * @brief Positions the iterator at the first (smallest) entry.
     *
     * After this call, valid() returns true if at least one entry
     * exists, false otherwise.
     */
    virtual void seek_to_first() = 0;

    /**
     * @brief Returns the outcome of the most recent traversal.
     *
     * Should be checked after a scan loop ends (i.e. when valid()
     * becomes false) to determine whether iteration completed
     * successfully or was terminated by an error.
     *
     * @return Success if the iterator exhausted all entries normally,
     *         or an error describing what went wrong (e.g. I/O
     *         failure, corrupted block).
     */
    virtual common::ExpectResult<void, common::Error> status() const = 0;

    /**
     * @brief Advances the iterator to the next entry.
     *
     * @pre valid() must be true before calling.
     *
     * After this call, valid() may become false if no more entries
     * remain or an error occurred.
     */
    virtual void next() = 0;

    /**
     * @brief Returns a reference to the current entry's encoded
     *        composite key.
     *
     * @pre valid() must be true.
     *
     * The reference is stable until the next call to next() or
     * seek_to_first().
     */
    virtual const std::vector<uint8_t>& key() const = 0;

    /**
     * @brief Returns a reference to the current entry's value
     *        (which may be a tombstone).
     *
     * @pre valid() must be true.
     *
     * The reference is stable until the next call to next() or
     * seek_to_first().
     */
    virtual const memtable::MemtableValue& value() const = 0;
};

};  // namespace enigmadb::storage

#endif  // ENIGMA_DB_ITERATOR_H
