#include "enigmadb/storage/dazzle_db/sstable/sstable_reader.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include "enigmadb/base.h"
#include "enigmadb/bloom_filter.h"
#include "enigmadb/crc32.h"
#include "enigmadb/encoding.h"
#include "enigmadb/error.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"
#include "enigmadb/storage/key.h"

namespace enigmadb::dazzle {

Result<SSTableReader> SSTableReader::create(io::IOEngine& engine,
                                            const std::string& path) {
    auto open_result = engine.open(path, io::Mode::Read);
    if (!open_result.has_value()) {
        return Result<SSTableReader>::err(open_result.error());
    }

    auto& fh = open_result.value();

    /* get file size */
    auto size_result = engine.file_size(fh);
    if (!size_result.has_value()) {
        return Result<SSTableReader>::err(size_result.error());
    }
    auto file_size = size_result.value();
    if (file_size < FOOTER_SIZE) {
        return Result<SSTableReader>::err(
            Error{ErrorCode::BAD_CONFIG, "file too small for SSTable"});
    }

    std::vector<uint8_t> footer_buffer;
    footer_buffer.resize(FOOTER_SIZE);

    /* read footer */
    auto read_footer_result = engine.read(fh, FOOTER_SIZE, footer_buffer.data(),
                                          file_size - FOOTER_SIZE);
    if (!read_footer_result.has_value()) {
        return Result<SSTableReader>::err(read_footer_result.error());
    }

    /* read and validate magic */
    if (std::memcmp(footer_buffer.data() + MAGIC_BYTES_OFFSET, MAGIC.data(),
                    MAGIC_SIZE)) {
        return Result<SSTableReader>::err(
            Error{ErrorCode::BAD_MAGIC, "invalid magic"});
    }

    auto stored_checksum =
        decode_uint32(footer_buffer.data(), FOOTER_CHECKSUM_OFFSET);
    auto computed_checksum =
        compute_crc_32(footer_buffer.data(), FOOTER_CHECKSUM_OFFSET);
    if (stored_checksum != computed_checksum) {
        return Result<SSTableReader>::err(
            Error{ErrorCode::BAD_CONFIG, "invalid checksum"});
    }

    /* extract details for index and read index */
    auto index_block_offset = decode_uint64(footer_buffer.data(), 0);
    auto index_block_size = decode_uint32(footer_buffer.data(), 8);

    /* extract details for filter and filter block */
    // TODO: Should create a two separate path here to validate offset for
    // filter block
    auto filter_block_offset = decode_uint64(footer_buffer.data(), 12);
    auto filter_block_size = decode_uint32(footer_buffer.data(), 20);
    if (filter_block_size < 2) {
        return Result<SSTableReader>::err(
            Error{ErrorCode::BAD_CONFIG, "invalid filter block size"});
    }

    auto entry_count = decode_uint32(footer_buffer.data(), 24);
    auto format_version = decode_uint16(footer_buffer.data(), 28);
    auto highest_sequence = decode_uint64(footer_buffer.data(), 30);
    auto size_bytes = decode_uint64(footer_buffer.data(), 38);

    /* construct footer */
    SSTFooter footer{index_block_offset, index_block_size, filter_block_offset,
                     filter_block_size,  entry_count,      format_version,
                     highest_sequence,   size_bytes};

    std::vector<uint8_t> buffer;
    buffer.resize(index_block_size + filter_block_size);

    auto read_result = engine.read(fh, index_block_size + filter_block_size,
                                   buffer.data(), index_block_offset);
    if (!read_result.has_value()) {
        return Result<SSTableReader>::err(read_result.error());
    }

    /* build the index entries */
    std::vector<IndexEntry> index_entries;
    size_t offset = 0;
    while (offset < index_block_size) {
        IndexEntry entry;
        if (offset + 4 > index_block_size) {
            return Result<SSTableReader>::err(
                Error{ErrorCode::READ_OUT_OF_RANGE, "key read out of range"});
        }
        auto key_len = decode_uint32(buffer.data(), offset);
        offset += 4;
        if (offset + key_len + 8 + 4 > index_block_size) {
            return Result<SSTableReader>::err(Error{
                ErrorCode::READ_OUT_OF_RANGE, "index entry out of range"});
        }
        entry.first_key.assign(buffer.data() + offset,
                               buffer.data() + offset + key_len);
        offset += key_len;
        entry.block_offset = decode_uint64(buffer.data(), offset);
        offset += 8;
        entry.block_size = decode_uint32(buffer.data(), offset);
        offset += 4;
        index_entries.push_back(std::move(entry));
    }

    /* extract details for the bloom filter */
    std::vector<uint8_t> bit_array;
    auto num_hashes = decode_uint8(buffer.data(), offset);
    offset += 1;
    bit_array.assign(buffer.data() + offset,
                     buffer.data() + offset + filter_block_size - 1);

    BloomFilter filter{bit_array, num_hashes};
    SSTableReader reader(engine, std::move(fh), path, std::move(index_entries),
                         filter, footer);
    return Result<SSTableReader>::ok(std::move(reader));
}

Result<std::optional<InternalValue>> SSTableReader::get(
    const storage::Key& key) {
    /* use the filter to determine if this key exists */
    if (!bloom_filter_.may_contain(key)) {
        return Result<std::optional<InternalValue>>::ok(std::nullopt);
    }

    /* binary search index to find the data block */
    auto it = std::upper_bound(
        index_entries_.begin(), index_entries_.end(), key,
        [&](const storage::Key& search_key, const IndexEntry& entry) {
            return search_key < entry.first_key;
        });

    if (it == index_entries_.begin()) {
        return Result<std::optional<InternalValue>>::ok(std::nullopt);
    }
    --it;

    /* read the data block, linear scan to find the key */
    std::vector<uint8_t> block_buffer(it->block_size);
    auto read_block_result = engine_.read(
        fh_, it->block_size, block_buffer.data(), it->block_offset);
    if (!read_block_result.has_value()) {
        return Result<std::optional<InternalValue>>::err(
            read_block_result.error());
    }

    size_t block_offset = 0;
    std::vector<uint8_t> current_value;
    while (block_offset < it->block_size) {
        current_value.clear();
        if (block_offset + 4 > it->block_size) {
            return Result<std::optional<InternalValue>>::err(
                Error{ErrorCode::BAD_FILE, "out of range read for key len"});
        }
        auto key_len = decode_uint32(block_buffer.data(), block_offset);
        block_offset += 4;
        if (block_offset + key_len > it->block_size) {
            return Result<std::optional<InternalValue>>::err(
                Error{ErrorCode::BAD_FILE, "out of range read for key"});
        }

        const uint8_t* kbeg = block_buffer.data() + block_offset;
        const uint8_t* kend = kbeg + key_len;
        auto ord = std::lexicographical_compare_three_way(
            key.bytes().begin(), key.bytes().end(), kbeg, kend);

        block_offset += key_len;
        /* value */
        if (block_offset + 4 > it->block_size) {
            return Result<std::optional<InternalValue>>::err(
                Error{ErrorCode::BAD_FILE, "out of range read for value len"});
        }
        auto value_len = decode_uint32(block_buffer.data(), block_offset);
        block_offset += 4;
        if (block_offset + value_len > it->block_size) {
            return Result<std::optional<InternalValue>>::err(
                Error{ErrorCode::BAD_FILE, "out of range read for value"});
        }
        if (ord == 0) {
            current_value.assign(
                block_buffer.data() + block_offset,
                block_buffer.data() + block_offset + value_len);
            block_offset += value_len;
            if (block_offset + 1 > it->block_size) {
                return Result<std::optional<InternalValue>>::err(Error{
                    ErrorCode::BAD_FILE, "out of range read for tombstone"});
            }
            auto tombstone = decode_uint8(block_buffer.data(), block_offset);
            block_offset += 1;

            if (block_offset + 8 > it->block_size) {
                return Result<std::optional<InternalValue>>::err(Error{
                    ErrorCode::BAD_FILE, "out of range read for sequence"});
            }
            auto sequence = decode_uint64(block_buffer.data(), block_offset);
            block_offset += 8;

            InternalValue value{current_value, static_cast<bool>(tombstone),
                                sequence};
            return Result<std::optional<InternalValue>>::ok(value);
        } else if (ord < 0) {
            break;
        } else {
            block_offset += value_len;
            if (block_offset + 9 > it->block_size) {
                return Result<std::optional<InternalValue>>::err(
                    Error{ErrorCode::UNEXPECTED_ERR, "Truncated entry."});
            }
            block_offset += 9;
        }
    }

    return Result<std::optional<InternalValue>>::ok(std::nullopt);
}

}  // namespace enigmadb::dazzle
