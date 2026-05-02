#include "enigmadb/storage/sstable/sstable_writer.h"

#include <vector>

#include "enigmadb/common/crc32.h"
#include "enigmadb/common/encoding.h"
#include "enigmadb/io/io_engine.h"

namespace enigmadb::storage::sstable {

SSTExpectResult<SSTableWriter> SSTableWriter::create(io::IOEngine& engine,
                                                     const std::string& path) {
    auto open_result = engine.open(path, io::Mode::Write);
    if (!open_result.has_value()) return open_result.err();
    auto& fh = open_result.value();
    SSTableWriter writer(engine, path, std::move(fh));
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
        auto flush_block_result = flush_block();
        if (!flush_block_result.has_value()) {
            return SSTExpectResult<void>::err(flush_block_result.err());
        }
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
    /* flush to disk */
    auto write_result = engine_.append(fh_, buffer_.data(), buffer_.size());
    if (!write_result.has_value()) {
        return SSTExpectResult<void>::err(write_result.err());
    }

    /* update index entries */
    index_entries_.push_back(IndexEntry{
        current_block_first_key_, current_block_start_offset_, buffer_.size()});
    current_file_offset_ += buffer_.size();

    current_block_first_key_.clear(); /* clear the first key */
    buffer_.clear(); /* clear keeps the mem allocated so good for us */
    return SSTExpectResult<void>::ok();
}

SSTExpectResult<void> SSTableWriter::finish() {
    if (!buffer_.empty()) {
        auto flush_block_result = flush_block();
        if (!flush_block_result.has_value()) {
            return SSTExpectResult<void>::err(flush_block_result.err());
        }
    }

    auto index_block_start = current_file_offset_;

    /* construct the index block */
    size_t index_size = 0;
    for (const auto& entry : index_entries_) {
        index_size += 4 + entry.first_key.size() + 8 + 4;
    }
    std::vector<uint8_t> index_buffer(index_size);
    size_t offset = 0;
    for (const auto& current_index : index_entries_) {
        auto keylen = current_index.first_key.size();
        offset = common::encode_uint32(keylen, index_buffer.data(), offset);
        offset = common::encode_bytes(current_index.first_key.data(), keylen,
                                      index_buffer.data(), offset);
        offset = common::encode_uint64(current_index.block_offset,
                                       index_buffer.data(), offset);
        offset = common::encode_uint32(current_index.block_size,
                                       index_buffer.data(), offset);
    }

    auto write_idx_block_result =
        engine_.append(fh_, index_buffer.data(), index_buffer.size());
    if (!write_idx_block_result.has_value()) {
        return SSTExpectResult<void>::err(write_idx_block_result.err());
    }

    /* construct the footer block */
    std::vector<uint8_t> footer_buffer;
    footer_buffer.resize(32);
    size_t footer_offset = 0;

    footer_offset = common::encode_uint64(index_block_start,
                                          footer_buffer.data(), footer_offset);
    footer_offset = common::encode_uint32(index_buffer.size(),
                                          footer_buffer.data(), footer_offset);
    footer_offset = common::encode_uint32(entry_count_, footer_buffer.data(),
                                          footer_offset);
    footer_offset = common::encode_uint16(SSTABLE_FORMAT_VERSION,
                                          footer_buffer.data(), footer_offset);

    /* calculate checksum */
    auto checksum = common::compute_crc_32(footer_buffer.data(), 18);
    footer_offset = common::encode_uint32(checksum, footer_buffer.data(),
                                          footer_offset); /* checksum */
    footer_offset =
        common::encode_bytes(MAGIC.data(), MAGIC_SIZE, footer_buffer.data(),
                             footer_offset); /* magic */
    footer_offset = common::encode_uint16(0, footer_buffer.data(),
                                          footer_offset); /* pad with 2 bytes */

    auto write_footer_block_result =
        engine_.append(fh_, footer_buffer.data(), footer_buffer.size());
    if (!write_footer_block_result.has_value()) {
        return SSTExpectResult<void>::err(write_footer_block_result.err());
    }

    /* flush this sstable to disk */
    auto sync_all_result = engine_.sync_all(fh_);
    if (!sync_all_result.has_value()) {
        return SSTExpectResult<void>::err(sync_all_result.err());
    }

    /* sync parent dir */
    std::string parent;
    auto slash_pos = path_.rfind("/");
    if (slash_pos == std::string::npos) {
        parent = ".";
    } else {
        parent = path_.substr(0, slash_pos);
    }
    auto sync_dir_result = engine_.sync_directory(parent);
    if (!sync_dir_result.has_value()) {
        return SSTExpectResult<void>::err(sync_dir_result.err());
    }

    return SSTExpectResult<void>::ok();
}

}  // namespace enigmadb::storage::sstable
