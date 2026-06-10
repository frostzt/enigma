/*
 * merge_iterator.h -- :)
 *
 * Author: frostzt
 * Date: 2026-06-03
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

struct HeapEntry {
    Iterator* source_;
};

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

class MergeIterator : public Iterator {
   public:
    explicit MergeIterator(std::vector<Iterator*> sources)
        : sources_(std::move(sources)), error_(std::nullopt), valid_(false) {}

    bool valid() const override;
    void seek_to_first() override;
    void next() override;
    const std::vector<uint8_t>& key() const override;
    const memtable::MemtableValue& value() const override;
    common::ExpectResult<void, common::Error> status() const override;

   private:
    std::vector<Iterator*> sources_;

    std::priority_queue<HeapEntry, std::vector<HeapEntry>, HeapCompare> heap_;

    std::optional<common::Error> error_;

    std::vector<uint8_t> current_key_;
    memtable::MemtableValue current_value_;
    bool valid_;

    void advance_to_winner();
    void advance_and_repush(Iterator* src);
};

}  // namespace enigmadb::storage

#endif  // ENIGMA_DB_MERGE_ITERATOR_H
