#include "enigmadb/storage/sstable/sstable_writer.h"

#include <vector>

#include "enigmadb/common/encoding.h"
#include "enigmadb/io/io_engine.h"

namespace enigmadb::storage::sstable {

SSTExpectResult<SSTableWriter> SSTableWriter::create(io::IOEngine& engine,
                                                     const std::string& path,
                                                     const size_t max_bytes) {
    auto open_result = engine.open(path, io::Mode::Write);
    if (!open_result.has_value()) return open_result.err();
    auto& fh = open_result.value();
    SSTableWriter writer(engine, path, std::move(fh), max_bytes);
    return SSTExpectResult<SSTableWriter>::ok(std::move(writer));
}

SSTExpectResult<void> SSTableWriter::add(const std::vector<uint8_t>& key,
                                         const memtable::MemtableValue& value) {
    auto key_len = key.size();
    auto value_len = value.data.size();
    auto required_size = /* key len */ 4 + /* key */ key_len +
                         /* value len */ 4 + /* value */ value_len +
                         /* is_tombstone */ 1;

    /* check if we need to flush and create a new one */
    if (buffer_.size() + required_size > MAX_PAGING_SIZE_BYTES) {
        flush_block();
    }

    /* set the first key for this entire data block */
    if (buffer_.empty()) {
        current_block_first_key_ = key;
        current_block_start_offset_ = current_file_offset_;
    }

    size_t offset = buffer_.size();
    buffer_.resize(offset + required_size);

    /* serialize data */
    offset = common::encode_uint32(key_len, buffer_.data(), offset);
    offset = common::encode_bytes(key.data(), key_len, buffer_.data(), offset);
    offset = common::encode_uint32(value_len, buffer_.data(), offset);
    offset = common::encode_bytes(value.data.data(), value_len, buffer_.data(),
                                  offset);
    offset = common::encode_uint8(value.is_tombstone, buffer_.data(), offset);

    entry_count_++;
    return SSTExpectResult<void>::ok();
}

SSTExpectResult<void> SSTableWriter::flush_block() {
    index_entries_.push_back(IndexEntry{
        current_block_first_key_, current_block_start_offset_, buffer_.size()});
    current_file_offset_ += buffer_.size();

    /* flush to disk */
    auto write_result = engine_.append(fh_, buffer_.data(), buffer_.size());
    if (!write_result.has_value()) {
        return SSTExpectResult<void>::err(write_result.err());
    }
    current_block_first_key_.clear(); /* clear the first key */
    buffer_.clear(); /* clear keeps the mem allocated so good for us */
    return SSTExpectResult<void>::ok();
}

}  // namespace enigmadb::storage::sstable
