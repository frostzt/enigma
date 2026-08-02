#include "enigmadb/storage/dazzle_db/sstable/sstable_writer.h"

#include <cassert>
#include <vector>

#include "enigmadb/crc32.h"
#include "enigmadb/encoding.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/dazzle_db/internal_value.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"
#include "enigmadb/storage/key.h"

namespace enigmadb::dazzle {

Result<SSTableWriter> SSTableWriter::create(io::IOEngine& engine,
                                            const std::string& path,
                                            size_t estimated_keys) {
    auto open_result = engine.open(path, io::Mode::Write);
    if (!open_result.has_value()) {
        return Result<SSTableWriter>::err(open_result.error());
    }

    auto& fh = open_result.value();
    SSTableWriter writer(engine, path, std::move(fh), estimated_keys);
    return Result<SSTableWriter>::ok(std::move(writer));
}

Result<void> SSTableWriter::add(const storage::Key& key,
                                const InternalValue& value) {
    auto key_len = key.size();
    auto value_len = value.data.size();
    auto required_size = /* key len */ 4 + /* key */ key_len +
                         /* value len */ 4 + /* value */ value_len +
                         /* is_tombstone */ 1 + /* sequence */ 8;

    /* check if we need to flush and create a new one */
    if (buffer_.size() + required_size > MAX_PAGING_SIZE_BYTES) {
        auto flush_block_result = flush_block();
        if (!flush_block_result.has_value()) {
            return Result<void>::err(flush_block_result.error());
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
    offset = encode_uint32(key_len, buffer_.data(), offset);
    offset = encode_bytes(key.bytes().data(), key_len, buffer_.data(), offset);
    offset = encode_uint32(value_len, buffer_.data(), offset);
    offset = encode_bytes(value.data.data(), value_len, buffer_.data(), offset);
    offset = encode_uint8(value.is_tombstone, buffer_.data(), offset);
    offset = encode_uint64(value.sequence, buffer_.data(), offset);

    /* add this key in the bloom filter */
    bloom_filter_.add(key);

    /* update the highest sequence stored */
    if (highest_sequence_ < value.sequence) {
        highest_sequence_ = value.sequence;
    }

    entry_count_++;
    return Result<void>::ok();
}

Result<void> SSTableWriter::flush_block() {
    /* flush to disk */
    auto write_result = engine_.append(fh_, buffer_.data(), buffer_.size());
    if (!write_result.has_value()) {
        return Result<void>::err(write_result.error());
    }

    if (write_result.value() != buffer_.size()) {
        return Result<void>::err(
            Error{ErrorCode::UNEXPECTED_ERR, "failed to write full block"});
    }

    /* update index entries */
    index_entries_.push_back(IndexEntry{
        current_block_first_key_, current_block_start_offset_, buffer_.size()});
    current_file_offset_ += buffer_.size();

    buffer_.clear(); /* clear keeps the mem allocated so good for us */
    return Result<void>::ok();
}

Result<void> SSTableWriter::finish() {
    if (!buffer_.empty()) {
        auto flush_block_result = flush_block();
        if (!flush_block_result.has_value()) {
            return Result<void>::err(flush_block_result.error());
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
        offset = encode_uint32(keylen, index_buffer.data(), offset);
        offset = encode_bytes(current_index.first_key.bytes().data(), keylen,
                              index_buffer.data(), offset);
        offset = encode_uint64(current_index.block_offset, index_buffer.data(),
                               offset);
        offset = encode_uint32(current_index.block_size, index_buffer.data(),
                               offset);
    }

    auto write_idx_block_result =
        engine_.append(fh_, index_buffer.data(), index_buffer.size());
    if (!write_idx_block_result.has_value()) {
        return Result<void>::err(write_idx_block_result.error());
    }

    if (write_idx_block_result.value() != index_buffer.size()) {
        return Result<void>::err(
            Error{ErrorCode::UNEXPECTED_ERR, "failed to write index block"});
    }

    /* construct the filter block */
    size_t filter_size = bloom_filter_.size_bytes();
    std::vector<uint8_t> filter_buffer(filter_size + 1);
    encode_uint8(bloom_filter_.num_hashes(), filter_buffer.data(), 0);
    encode_bytes(bloom_filter_.data().data(), filter_size, filter_buffer.data(),
                 1);

    auto write_filter_block_result =
        engine_.append(fh_, filter_buffer.data(), filter_buffer.size());
    if (!write_filter_block_result.has_value()) {
        return Result<void>::err(write_filter_block_result.error());
    }

    if (write_filter_block_result.value() != filter_buffer.size()) {
        return Result<void>::err(
            Error{ErrorCode::UNEXPECTED_ERR, "failed to write filter block"});
    }

    /* construct the footer block */
    std::vector<uint8_t> footer_buffer;
    footer_buffer.resize(56);
    size_t footer_offset = 0;

    footer_offset =
        encode_uint64(index_block_start, footer_buffer.data(), footer_offset);
    footer_offset =
        encode_uint32(index_buffer.size(), footer_buffer.data(), footer_offset);
    footer_offset = encode_uint64(index_block_start + index_buffer.size(),
                                  footer_buffer.data(), footer_offset);
    footer_offset = encode_uint32(filter_buffer.size(), footer_buffer.data(),
                                  footer_offset);
    footer_offset =
        encode_uint32(entry_count_, footer_buffer.data(), footer_offset);
    footer_offset = encode_uint16(SSTABLE_FORMAT_VERSION, footer_buffer.data(),
                                  footer_offset);
    footer_offset =
        encode_uint64(highest_sequence_, footer_buffer.data(), footer_offset);

    /* calculate checksum */
    auto checksum = compute_crc_32(footer_buffer.data(), 38);
    footer_offset = encode_uint32(checksum, footer_buffer.data(),
                                  footer_offset); /* checksum */
    footer_offset = encode_bytes(MAGIC.data(), MAGIC_SIZE, footer_buffer.data(),
                                 footer_offset); /* magic */
    /* pad with 6 bytes */
    footer_offset = encode_uint32(0, footer_buffer.data(), footer_offset);
    footer_offset = encode_uint16(0, footer_buffer.data(), footer_offset);

    auto write_footer_block_result =
        engine_.append(fh_, footer_buffer.data(), footer_buffer.size());
    if (!write_footer_block_result.has_value()) {
        return Result<void>::err(write_footer_block_result.error());
    }

    /* flush this sstable to disk */
    auto sync_all_result = engine_.sync_all(fh_);
    if (!sync_all_result.has_value()) {
        return Result<void>::err(sync_all_result.error());
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
        return Result<void>::err(sync_dir_result.error());
    }

    return Result<void>::ok();
}

}  // namespace enigmadb::dazzle
