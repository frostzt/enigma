#include "enigmadb/storage/sstable/sstable_reader.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>

#include "enigmadb/common/bloom_filter.h"
#include "enigmadb/common/crc32.h"
#include "enigmadb/common/encoding.h"
#include "enigmadb/common/error.h"
#include "enigmadb/storage/key_encoding.h"
#include "enigmadb/storage/memtable/memtable.h"
#include "enigmadb/storage/sstable/sstable_common.h"

namespace enigmadb::storage::sstable {

SSTExpectResult<SSTableReader> SSTableReader::create(io::IOEngine& engine,
                                                     const std::string& path) {
    auto open_result = engine.open(path, io::Mode::Read);
    if (!open_result.has_value()) return open_result.err();
    auto& fh = open_result.value();

    /* get file size */
    auto size_result = engine.file_size(fh);
    if (!size_result.has_value()) {
        return SSTExpectResult<SSTableReader>::err(size_result.err());
    }
    auto file_size = size_result.value();
    if (file_size < 48) {
        return SSTExpectResult<SSTableReader>::err(common::Error{
            common::ErrorCode::BAD_CONFIG, "file too small for SSTable"});
    }

    std::vector<uint8_t> footer_buffer;
    footer_buffer.resize(48);

    /* read footer */
    auto read_footer_result =
        engine.read(fh, 48, footer_buffer.data(), file_size - 48);
    if (!read_footer_result.has_value()) {
        return SSTExpectResult<SSTableReader>::err(read_footer_result.err());
    }

    /* read and validate magic */
    if (std::memcmp(footer_buffer.data() + 34, MAGIC.data(), MAGIC_SIZE)) {
        return SSTExpectResult<SSTableReader>::err(
            common::Error{common::ErrorCode::BAD_MAGIC, "invalid magic"});
    }

    auto stored_checksum = common::decode_uint32(footer_buffer.data(), 30);
    auto computed_checksum = common::compute_crc_32(footer_buffer.data(), 30);
    if (stored_checksum != computed_checksum) {
        return SSTExpectResult<SSTableReader>::err(
            common::Error{common::ErrorCode::BAD_CONFIG, "invalid checksum"});
    }

    /* extract details for index and read index */
    auto index_block_offset = common::decode_uint64(footer_buffer.data(), 0);
    auto index_block_size = common::decode_uint32(footer_buffer.data(), 8);

    /* extract details for filter and filter block */
    // TODO: Should create a two separate path here to validate offset for
    // filter block
    auto filter_block_size = common::decode_uint32(footer_buffer.data(), 20);

    if (filter_block_size < 2) {
        return SSTExpectResult<SSTableReader>::err(common::Error{
            common::ErrorCode::BAD_CONFIG, "invalid filter block size"});
    }

    std::vector<uint8_t> buffer;
    buffer.resize(index_block_size + filter_block_size);

    auto read_result = engine.read(fh, index_block_size + filter_block_size,
                                   buffer.data(), index_block_offset);
    if (!read_result.has_value()) {
        return SSTExpectResult<SSTableReader>::err(read_result.err());
    }

    /* build the index entries */
    std::vector<IndexEntry> index_entries;
    size_t offset = 0;
    while (offset < index_block_size) {
        IndexEntry entry;
        if (offset + 4 > index_block_size) {
            return SSTExpectResult<SSTableReader>::err(common::Error{
                common::ErrorCode::READ_OUT_OF_RANGE, "key read out of range"});
        }
        auto key_len = common::decode_uint32(buffer.data(), offset);
        offset += 4;
        if (offset + key_len + 8 + 4 > index_block_size) {
            return SSTExpectResult<SSTableReader>::err(
                common::Error{common::ErrorCode::READ_OUT_OF_RANGE,
                              "index entry out of range"});
        }
        entry.first_key.assign(buffer.data() + offset,
                               buffer.data() + offset + key_len);
        offset += key_len;
        entry.block_offset = common::decode_uint64(buffer.data(), offset);
        offset += 8;
        entry.block_size = common::decode_uint32(buffer.data(), offset);
        offset += 4;
        index_entries.push_back(std::move(entry));
    }

    /* extract details for the bloom filter */
    std::vector<uint8_t> bit_array;
    auto num_hashes = common::decode_uint8(buffer.data(), offset);
    offset += 1;
    bit_array.assign(buffer.data() + offset,
                     buffer.data() + offset + filter_block_size - 1);

    common::BloomFilter filter{bit_array, num_hashes};
    SSTableReader reader(engine, std::move(fh), path, std::move(index_entries),
                         filter);
    return SSTExpectResult<SSTableReader>::ok(std::move(reader));
}

SSTExpectResult<std::optional<memtable::MemtableValue>> SSTableReader::get(
    const std::vector<uint8_t>& key) {
    /* use the filter to determine if this key exists */
    if (!bloom_filter_.may_contain(key)) {
        return SSTExpectResult<std::optional<memtable::MemtableValue>>::ok(
            std::nullopt);
    }

    /* binary search index to find the data block */
    CompositeKeyComparator cmp;
    auto it =
        std::upper_bound(index_entries_.begin(), index_entries_.end(), key,
                         [&cmp](const std::vector<uint8_t>& search_key,
                                const IndexEntry& entry) {
                             return cmp(search_key, entry.first_key);
                         });

    if (it == index_entries_.begin()) {
        return SSTExpectResult<std::optional<memtable::MemtableValue>>::ok(
            std::nullopt);
    }
    --it;

    /* read the data block, linear scan to find the key */
    std::vector<uint8_t> block_buffer(it->block_size);
    auto read_block_result = engine_.read(
        fh_, it->block_size, block_buffer.data(), it->block_offset);
    if (!read_block_result.has_value()) {
        return SSTExpectResult<std::optional<memtable::MemtableValue>>::err(
            read_block_result.err());
    }

    size_t block_offset = 0;
    std::vector<uint8_t> current_key;
    std::vector<uint8_t> current_value;
    while (block_offset < it->block_size) {
        current_key.clear();
        current_value.clear();
        if (block_offset + 4 > it->block_size) {
            return SSTExpectResult<std::optional<memtable::MemtableValue>>::err(
                common::Error{common::ErrorCode::BAD_FILE,
                              "out of range read for key len"});
        }
        auto key_len = common::decode_uint32(block_buffer.data(), block_offset);
        block_offset += 4;
        if (block_offset + key_len > it->block_size) {
            return SSTExpectResult<std::optional<memtable::MemtableValue>>::err(
                common::Error{common::ErrorCode::BAD_FILE,
                              "out of range read for key"});
        }
        current_key.assign(block_buffer.data() + block_offset,
                           block_buffer.data() + block_offset + key_len);
        block_offset += key_len;
        /* value */
        if (block_offset + 4 > it->block_size) {
            return SSTExpectResult<std::optional<memtable::MemtableValue>>::err(
                common::Error{common::ErrorCode::BAD_FILE,
                              "out of range read for value len"});
        }
        auto value_len =
            common::decode_uint32(block_buffer.data(), block_offset);
        block_offset += 4;
        if (block_offset + value_len > it->block_size) {
            return SSTExpectResult<std::optional<memtable::MemtableValue>>::err(
                common::Error{common::ErrorCode::BAD_FILE,
                              "out of range read for value"});
        }
        current_value.assign(block_buffer.data() + block_offset,
                             block_buffer.data() + block_offset + value_len);
        block_offset += value_len;
        if (block_offset + 1 > it->block_size) {
            return SSTExpectResult<std::optional<memtable::MemtableValue>>::err(
                common::Error{common::ErrorCode::BAD_FILE,
                              "out of range read for tombstone"});
        }
        auto tombstone =
            common::decode_uint8(block_buffer.data(), block_offset);
        block_offset += 1;

        if (block_offset + 8 > it->block_size) {
            return SSTExpectResult<std::optional<memtable::MemtableValue>>::err(
                common::Error{common::ErrorCode::BAD_FILE,
                              "out of range read for sequence"});
        }
        auto sequence =
            common::decode_uint64(block_buffer.data(), block_offset);
        block_offset += 8;

        /* eqality: neither a < b nor b < a which means a == b */
        if (!cmp(key, current_key) && !cmp(current_key, key)) {
            memtable::MemtableValue value{
                current_value, static_cast<bool>(tombstone), sequence};
            return SSTExpectResult<std::optional<memtable::MemtableValue>>::ok(
                value);
        } else if (cmp(key, current_key)) {
            break;
        }
    }

    return SSTExpectResult<std::optional<memtable::MemtableValue>>::ok(
        std::nullopt);
}

}  // namespace enigmadb::storage::sstable
