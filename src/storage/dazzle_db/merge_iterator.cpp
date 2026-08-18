#include "enigmadb/storage/dazzle_db/merge_iterator.h"

#include <cassert>
#include <optional>
#include <vector>

#include "enigmadb/storage/dazzle_db/internal_iterator.h"
#include "enigmadb/storage/dazzle_db/internal_value.h"
#include "enigmadb/storage/key.h"

namespace enigmadb::dazzle {

bool MergeIterator::valid() const { return valid_; }

const storage::Key& MergeIterator::key() const {
    assert(valid());
    return current_key_;
}

const InternalValue& MergeIterator::value() const {
    assert(valid());
    return current_value_;
}

void MergeIterator::set_error(Error err) {
    error_ = std::move(err);
    valid_ = false;
    reset_heap();
}

bool MergeIterator::is_error() const { return error_.has_value(); }

void MergeIterator::reset_heap() { heap_ = std::priority_queue<HeapEntry, std::vector<HeapEntry>, HeapCompare>(); }

void MergeIterator::seek_to_first() {
    /* clear the heap */
    reset_heap();
    error_ = std::nullopt;
    valid_ = false;
    current_value_.data.clear();
    current_value_.is_tombstone = false;
    current_value_.sequence = 0;

    /* advance all sources */
    for (auto source : sources_) {
        source->seek_to_first();
        if (source->valid()) {
            heap_.push(HeapEntry{source});
        } else if (!source->status().has_value()) {
            set_error(source->status().error());
            break;
        }
    }

    /* get the latest and copy over */
    if (!is_error()) {
        advance_to_winner();
    }
}

Result<void> MergeIterator::status() const {
    if (error_.has_value()) {
        return ExpectResult<void, Error>::err(error_.value());
    }
    return ExpectResult<void, Error>::ok();
}

void MergeIterator::advance_to_winner() {
    if (heap_.empty()) {
        valid_ = false;
        return;
    }

    valid_ = true;

    /* get value from top and copy */
    auto heap_entry = heap_.top();
    current_key_ = heap_entry.source_->key();
    current_value_ = heap_entry.source_->value();
    heap_.pop();

    /* advance the iterator */
    advance_and_repush(heap_entry.source_);

    /* deduplicate entries based on the current key and the key
     * on the top of the heap.
     *
     * WHY IS THIS SAFE? Deterministic encoding.
     * The key encoding is quite trivial which is
     * partition key + clustering key + column name
     *
     * In every case no matter insert/update/delete those three
     * will always be present which makes the keys identical and
     * therefore quite trivial to match.
     */
    while (!heap_.empty() && heap_.top().source_->key() == current_key_) {
        auto top = heap_.top();
        heap_.pop();
        advance_and_repush(top.source_);
    }
}

void MergeIterator::advance_and_repush(InternalIterator* src) {
    src->next();
    if (src->valid()) {
        heap_.push(HeapEntry{src});
    } else if (!src->status().has_value()) { /* we have an actual error */
        set_error(src->status().error());
    }
}

void MergeIterator::next() { advance_to_winner(); }

}  // namespace enigmadb::dazzle
