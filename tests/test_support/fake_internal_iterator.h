#ifndef ENIGMA_DB_TEST_SUPPORT_FAKE_INTERNAL_ITERATOR_H
#define ENIGMA_DB_TEST_SUPPORT_FAKE_INTERNAL_ITERATOR_H

#include <cassert>
#include <utility>
#include <vector>

#include "enigmadb/storage/dazzle_db/internal_iterator.h"
#include "enigmadb/storage/dazzle_db/internal_value.h"
#include "enigmadb/storage/key.h"

namespace enigmadb::TESTNAMESPACE {

class FakeInternalIterator : public dazzle::InternalIterator {
   public:
    using Entry = std::pair<storage::Key, dazzle::InternalValue>;

    explicit FakeInternalIterator(std::vector<Entry> entries) : entries_(std::move(entries)) {}

    bool valid() const override { return curr_idx_ < entries_.size(); }
    void seek_to_first() override { curr_idx_ = 0; }
    void next() override { curr_idx_++; }

    const storage::Key& key() const override {
        assert(valid());
        return entries_[curr_idx_].first;
    }

    const dazzle::InternalValue& value() const override {
        assert(valid());
        return entries_[curr_idx_].second;
    }

    Result<void> status() const override { return Result<void>::ok(); }

   private:
    std::vector<Entry> entries_;
    size_t curr_idx_ = 0;
};

}  // namespace enigmadb::TESTNAMESPACE

#endif
