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
#include "enigmadb/storage/key.h"
#include "enigmadb/storage/value.h"

namespace enigmadb::storage {

class FakeIterator : public Iterator {
   public:
    FakeIterator(std::vector<std::pair<Key, Value>> entries)
        : entries_(std::move(entries)), curr_idx_(0) {}

    bool valid() const override { return curr_idx_ < entries_.size(); }

    void seek_to_first() override { curr_idx_ = 0; }

    void next() override { curr_idx_++; }

    Result<void> status() const override { return Result<void>::ok(); }

    /// @copydoc Iterator::key()
    const Key& key() const override {
        assert(valid());
        return entries_[curr_idx_].first;
    }

    /// @copydoc Iterator::value()
    const Value& value() const override {
        assert(valid());
        return entries_[curr_idx_].second;
    }

   private:
    std::vector<std::pair<Key, Value>> entries_;
    size_t curr_idx_;
};

};  // namespace enigmadb::storage

#endif  // ENIGMA_DB_FAKE_ITERATOR_H
