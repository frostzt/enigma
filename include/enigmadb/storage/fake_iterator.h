/**
 * @file fake_iterator.h
 * @brief Minimal single-entry iterator stub for unit testing.
 *
 * FakeIterator holds exactly one key-value pair and is always valid.
 * It does not implement seek_to_first(), next(), status(), or valid()
 * only key() and value() are usable. This is intentional: it exists
 * solely to feed a known entry into components under test (e.g.
 * MergeIterator) without requiring real SSTable or memtable backing.
 *
 * @note Test-only — do not use in production code.
 *
 * @author frostzt
 * @date 2026-06-03
 */

#ifndef ENIGMA_DB_FAKE_ITERATOR_H
#define ENIGMA_DB_FAKE_ITERATOR_H

#include <vector>

#include "enigmadb/storage/iterator.h"
#include "enigmadb/storage/memtable/memtable.h"

namespace enigmadb::storage::sstable {

/**
 * @brief Test stub that exposes a single hardcoded key-value pair
 *        through the Iterator interface.
 *
 * Only key() and value() are implemented. The remaining Iterator
 * methods (valid, seek_to_first, next, status) are left unoverridden
 * and will trigger a linker or pure-virtual error if called — this
 * keeps the stub minimal and makes accidental misuse obvious.
 */
class FakeIterator : public Iterator {
   public:
    /**
     * @brief Constructs a fake iterator holding a single entry.
     *
     * @param key    Encoded composite key.
     * @param value  Associated memtable value (may be a tombstone).
     */
    FakeIterator(std::vector<uint8_t> key, memtable::MemtableValue value)
        : key_(std::move(key)), value_(std::move(value)) {}

    bool valid() const override { return true; }
    void seek_to_first() override {}
    void next() override {}
    common::ExpectResult<void, common::Error> status() const override {
        return common::ExpectResult<void, common::Error>::ok();
    }

    /// @copydoc Iterator::key()
    const std::vector<uint8_t>& key() const override { return key_; }

    /// @copydoc Iterator::value()
    const memtable::MemtableValue& value() const override { return value_; }

   private:
    std::vector<uint8_t> key_;       ///< The single stored key.
    memtable::MemtableValue value_;  ///< The single stored value.
};

};  // namespace enigmadb::storage::sstable

#endif  // ENIGMA_DB_FAKE_ITERATOR_H
