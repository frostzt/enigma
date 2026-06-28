/**
 * @file fake_iterator.h
 * @brief Minimal single-entry iterator stub for unit testing.
 * @note Test-only — do not use in production code.
 *
 * @author frostzt
 * @date 2026-06-03
 */

#ifndef ENIGMA_DB_FAKE_ITERATOR_H
#define ENIGMA_DB_FAKE_ITERATOR_H

#include <cassert>
#include <utility>
#include <vector>

#include "enigmadb/storage/iterator.h"
#include "enigmadb/storage/memtable/memtable.h"

namespace enigmadb::storage {

using MemtableValue = memtable::MemtableValue;

class FakeIterator : public Iterator {
   public:
    FakeIterator(
        std::vector<std::pair<std::vector<uint8_t>, MemtableValue>> entries)
        : entries_(std::move(entries)), curr_idx_(0) {}

    bool valid() const override { return curr_idx_ < entries_.size(); }

    void seek_to_first() override { curr_idx_ = 0; }

    void next() override { curr_idx_++; }

    common::ExpectResult<void, common::Error> status() const override {
        return common::ExpectResult<void, common::Error>::ok();
    }

    /// @copydoc Iterator::key()
    const std::vector<uint8_t>& key() const override {
        assert(valid());
        return entries_[curr_idx_].first;
    }

    /// @copydoc Iterator::value()
    const memtable::MemtableValue& value() const override {
        assert(valid());
        return entries_[curr_idx_].second;
    }

   private:
    std::vector<std::pair<std::vector<uint8_t>, MemtableValue>> entries_;
    size_t curr_idx_;
};

};  // namespace enigmadb::storage

#endif  // ENIGMA_DB_FAKE_ITERATOR_H
