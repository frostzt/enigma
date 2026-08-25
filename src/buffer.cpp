#include "enigmadb/buffer.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "enigmadb/encoding.h"
#include "enigmadb/error.h"
#include "enigmadb/log.h"

namespace enigmadb {

/* --------------------------
 *  BUFFER READER
 * -------------------------- */

size_t BufferReader::remaining() const {
    assert(length_ >= pos_);
    return length_ - pos_;
}

size_t BufferReader::consumed() const { return pos_; }

bool BufferReader::ok() const { return !err_.has_value(); }

const Error& BufferReader::error() const { return err_.value(); }

uint64_t BufferReader::read_u64() {
    if (!ok()) return 0;
    if (remaining() < 8) {
        err_ = Error::out_of_range("wanted to read 8 bytes but " + std::to_string(remaining()) + " remaining");
        return 0;
    }

    auto value = decode_uint64(data_, pos_);
    pos_ += 8;
    return value;
}

uint32_t BufferReader::read_u32() {
    if (!ok()) return 0;
    if (remaining() < 4) {
        err_ = Error::out_of_range("wanted to read 4 bytes but " + std::to_string(remaining()) + " remaining");
        return 0;
    }
    auto value = decode_uint32(data_, pos_);
    pos_ += 4;
    return value;
}

uint16_t BufferReader::read_u16() {
    if (!ok()) return 0;
    if (remaining() < 2) {
        err_ = Error::out_of_range("wanted to read 2 bytes but " + std::to_string(remaining()) + " remaining");
        return 0;
    }
    auto value = decode_uint16(data_, pos_);
    pos_ += 2;
    return value;
}

uint8_t BufferReader::read_u8() {
    if (!ok()) return 0;
    if (remaining() < 1) {
        err_ = Error::out_of_range("wanted to read 1 bytes but " + std::to_string(remaining()) + " remaining");
        return 0;
    }
    auto value = decode_uint8(data_, pos_);
    pos_ += 1;
    return value;
}

std::span<const uint8_t> BufferReader::read_bytes(size_t n) {
    if (!ok()) return {};
    if (remaining() < n) {
        err_ = Error::out_of_range("wanted to read " + std::to_string(n) + " bytes but " + std::to_string(remaining()) +
                                   " remaining");
        return {};
    }

    std::span<const uint8_t> result(data_ + pos_, n);
    pos_ += n;
    return result;
}

void BufferReader::skip(size_t n) {
    if (!ok()) return;
    if (remaining() < n) {
        err_ = Error::out_of_range("wanted to skip over " + std::to_string(n) + " bytes but " +
                                   std::to_string(remaining()) + " remaining");
        return;
    }
    pos_ += n;
}

size_t BufferReader::buffer_length() const { return length_; }

BufferReader BufferReader::sub(size_t n) {
    if (!ok()) return BufferReader(data_ + pos_, 0, err_);
    if (remaining() < n) {
        LOG_TRACE(Category::General, "Tried to create a new sub buffer reader over bytes extending available space");
        err_ = Error::out_of_range("wanted to create a view over " + std::to_string(n) + " bytes but " +
                                   std::to_string(remaining()) + " remaining");
        return BufferReader(data_ + pos_, 0, err_);
    }

    BufferReader reader(data_ + pos_, n);
    pos_ += n;
    return reader;
}

/* --------------------------
 *  BUFFER WRITER
 * -------------------------- */

bool BufferWriter::ok() const { return !err_.has_value(); }

const Error& BufferWriter::error() const { return err_.value(); }

size_t BufferWriter::size() const { return data_.size(); }

size_t BufferWriter::capacity() const { return data_.capacity(); }

void BufferWriter::write_u8(uint8_t value) {
    if (!ok()) return;
    auto off = data_.size();
    data_.resize(off + 1);
    encode_uint8(value, data_.data(), off);
}

void BufferWriter::write_u16(uint16_t value) {
    if (!ok()) return;
    auto off = data_.size();
    data_.resize(off + 2);
    encode_uint16(value, data_.data(), off);
}

void BufferWriter::write_u32(uint32_t value) {
    if (!ok()) return;
    auto off = data_.size();
    data_.resize(off + 4);
    encode_uint32(value, data_.data(), off);
}

void BufferWriter::write_u64(uint64_t value) {
    if (!ok()) return;
    auto off = data_.size();
    data_.resize(off + 8);
    encode_uint64(value, data_.data(), off);
}

void BufferWriter::write_bytes(std::span<const uint8_t> value) {
    if (!ok()) return;
    data_.insert(data_.end(), value.begin(), value.end());
}

size_t BufferWriter::reserve_slot(size_t n) {
    if (!ok()) return 0;
    data_.resize(size() + n);
    return data_.size() - n;
}

void BufferWriter::patch_u8(size_t offset, uint8_t value) {
    if (!ok()) return;
    if (offset > size() || size() - offset < 1) {
        err_ = Error::out_of_range("Trying to patch byte at offset " + std::to_string(offset) +
                                   " which is invalid access");
        return;
    }
    encode_uint8(value, data_.data(), offset);
}

void BufferWriter::patch_u16(size_t offset, uint16_t value) {
    if (!ok()) return;
    if (auto sz = data_.size(); offset > sz || sz - offset < 2) {
        err_ = Error::out_of_range("Trying to patch byte at offset " + std::to_string(offset) +
                                   " which is invalid access");
        return;
    }
    encode_uint16(value, data_.data(), offset);
}

void BufferWriter::patch_u32(size_t offset, uint32_t value) {
    if (!ok()) return;
    if (auto sz = data_.size(); offset > sz || sz - offset < 4) {
        err_ = Error::out_of_range("Trying to patch byte at offset " + std::to_string(offset) +
                                   " which is invalid access");
        return;
    }
    encode_uint32(value, data_.data(), offset);
}

void BufferWriter::patch_u64(size_t offset, uint64_t value) {
    if (!ok()) return;
    if (auto sz = data_.size(); offset > sz || sz - offset < 8) {
        err_ = Error::out_of_range("Trying to patch byte at offset " + std::to_string(offset) +
                                   " which is invalid access");
        return;
    }
    encode_uint64(value, data_.data(), offset);
}

void BufferWriter::truncate(size_t mark, bool clear_error) {
    if (mark > size()) {
        err_ = Error::out_of_range("Truncation failed as the mark provided exceeds the current size");
        return;
    }

    data_.resize(mark);

    /* Defaults to true. Generally the idea is that the writer truncates post a bad write
     * that leaves the writer poisoned */
    if (clear_error) {
        err_ = std::nullopt;
    }
}

void BufferWriter::clear() {
    err_ = std::nullopt;
    data_.clear();
}

std::span<const uint8_t> BufferWriter::data() const { return std::span<const uint8_t>{data_}; }

void BufferWriter::reserve(size_t alloc) { data_.reserve(alloc); }

}  // namespace enigmadb
