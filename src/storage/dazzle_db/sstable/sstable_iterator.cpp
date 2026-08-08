#include "enigmadb/storage/dazzle_db/sstable/sstable_iterator.h"

#include <cassert>
#include <vector>

#include "enigmadb/encoding.h"
#include "enigmadb/storage/key.h"

namespace enigmadb::dazzle {

bool SSTableIterator::valid() const { return valid_; }

const storage::Key& SSTableIterator::key() const {
    assert(valid());
    return current_key_;
}

const InternalValue& SSTableIterator::value() const {
    assert(valid());
    return current_value_;
}

ExpectResult<void, Error> SSTableIterator::status() const {
    if (error_.has_value()) {
        return ExpectResult<void, Error>::err(error_.value());
    }
    return ExpectResult<void, Error>::ok();
}

void SSTableIterator::set_error(Error err) {
    error_ = std::move(err);
    valid_ = false;
}

bool SSTableIterator::load_block() {
    const auto& entry = index_entries_[current_block_idx_];
    block_buffer_.resize(entry.block_size);
    block_offset_ = 0;

    auto res = engine_.read(fh_, entry.block_size, block_buffer_.data(),
                            entry.block_offset);
    if (!res.has_value()) {
        set_error(res.error());
        return false;
    }
    return true;
}

void SSTableIterator::seek_to_first() {
    current_block_idx_ = 0;
    block_buffer_.clear();
    block_offset_ = 0;
    error_ = std::nullopt;
    valid_ = false;

    if (index_entries_.empty()) {
        return;
    }

    if (!load_block()) {
        return;
    }

    next();
}

void SSTableIterator::next() {
    if (error_.has_value()) return;

    /* exhausted the current block, move to the next one */
    if (!block_buffer_.empty() &&
        block_offset_ >= index_entries_[current_block_idx_].block_size) {
        current_block_idx_++;

        /* no more blocks, iteration complete */
        if (current_block_idx_ >= index_entries_.size()) {
            valid_ = false;
            return;
        }

        if (!load_block()) {
            return;
        }
    }

    /* first call from seek_to_first, block already loaded */
    if (block_buffer_.empty()) {
        valid_ = false;
        return;
    }

    auto block_size = index_entries_[current_block_idx_].block_size;

    /* extract the key */
    if (block_offset_ + 4 > block_size) {
        set_error(
            Error{ErrorCode::BAD_FILE, "out of range read for key length"});
        return;
    }

    auto key_len = decode_uint32(block_buffer_.data(), block_offset_);
    block_offset_ += 4;

    if (block_offset_ + key_len > block_size) {
        set_error(Error{ErrorCode::BAD_FILE, "out of range read for key"});
        return;
    }

    /* assign key as the current key */
    current_key_.assign(block_buffer_.data() + block_offset_,
                        block_buffer_.data() + block_offset_ + key_len);

    block_offset_ += key_len;

    /* extract the value */
    if (block_offset_ + 4 > block_size) {
        set_error(
            Error{ErrorCode::BAD_FILE, "out of range read for value length"});
        return;
    }

    auto v_len = decode_uint32(block_buffer_.data(), block_offset_);
    block_offset_ += 4;

    if (block_offset_ + v_len > block_size) {
        set_error(Error{ErrorCode::BAD_FILE, "out of range read for value"});
        return;
    }

    /* assign value as the current value */
    current_value_.data.assign(block_buffer_.data() + block_offset_,
                               block_buffer_.data() + block_offset_ + v_len);

    block_offset_ += v_len;

    /* decode tombstone flag */
    if (block_offset_ + 1 > block_size) {
        set_error(
            Error{ErrorCode::BAD_FILE, "out of range read for tombstone"});
        return;
    }

    current_value_.is_tombstone =
        decode_uint8(block_buffer_.data(), block_offset_);
    block_offset_ += 1;

    /* decode sequence */
    if (block_offset_ + 8 > block_size) {
        set_error(Error{ErrorCode::BAD_FILE, "out of range read for sequence"});
        return;
    }

    current_value_.sequence =
        decode_uint64(block_buffer_.data(), block_offset_);
    block_offset_ += 8;

    valid_ = true;
}

};  // namespace enigmadb::dazzle
