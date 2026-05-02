#include "enigmadb/storage/sstable/sstable_reader.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>

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
    if (file_size < 32) {
        return SSTExpectResult<SSTableReader>::err(common::Error{
            common::ErrorCode::BAD_CONFIG, "file too small for SSTable"});
    }

    std::vector<uint8_t> footer_buffer;
    footer_buffer.resize(32);

    /* read footer */
    auto read_footer_result =
        engine.read(fh, 32, footer_buffer.data(), file_size - 32);
    if (!read_footer_result.has_value()) {
        return SSTExpectResult<SSTableReader>::err(read_footer_result.err());
    }

    /* read and validate magic */
    if (std::memcmp(footer_buffer.data() + 22, MAGIC.data(), MAGIC_SIZE)) {
        return SSTExpectResult<SSTableReader>::err(
            common::Error{common::ErrorCode::BAD_MAGIC, "invalid magic"});
    }

    auto stored_checksum = common::decode_uint32(footer_buffer.data(), 18);
    auto computed_checksum = common::compute_crc_32(footer_buffer.data(), 18);
    if (stored_checksum != computed_checksum) {
        return SSTExpectResult<SSTableReader>::err(
            common::Error{common::ErrorCode::BAD_CONFIG, "invalid checksum"});
    }

    /* extract details for index and read index */
    auto index_block_offset = common::decode_uint64(footer_buffer.data(), 0);
    auto index_block_size = common::decode_uint32(footer_buffer.data(), 8);

    std::vector<uint8_t> index_buffer;
    index_buffer.resize(index_block_size);

    auto read_index_result = engine.read(
        fh, index_block_size, index_buffer.data(), index_block_offset);
    if (!read_index_result.has_value()) {
        return SSTExpectResult<SSTableReader>::err(read_index_result.err());
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
        auto key_len = common::decode_uint32(index_buffer.data(), offset);
        offset += 4;
        if (offset + key_len + 8 + 4 > index_block_size) {
            break;
        }
        entry.first_key.assign(index_buffer.data() + offset,
                               index_buffer.data() + offset + key_len);
        offset += key_len;
        entry.block_offset = common::decode_uint64(index_buffer.data(), offset);
        offset += 8;
        entry.block_size = common::decode_uint32(index_buffer.data(), offset);
        offset += 4;
        index_entries.push_back(std::move(entry));
    }

    SSTableReader reader(engine, std::move(fh), path, std::move(index_entries));
    return SSTExpectResult<SSTableReader>::ok(std::move(reader));
}

SSTExpectResult<std::optional<memtable::MemtableValue>> SSTableReader::get(
    const std::vector<uint8_t>& key) {
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
        auto key_len = common::decode_uint32(block_buffer.data(), block_offset);
        block_offset += 4;
        current_key.assign(block_buffer.data() + block_offset,
                           block_buffer.data() + block_offset + key_len);
        block_offset += key_len;
        /* value */
        auto value_len =
            common::decode_uint32(block_buffer.data(), block_offset);
        block_offset += 4;
        current_value.assign(block_buffer.data() + block_offset,
                             block_buffer.data() + block_offset + value_len);
        block_offset += value_len;
        auto tombstone =
            common::decode_uint8(block_buffer.data(), block_offset);
        block_offset += 1;

        /* eqality: neither a < b nor b < a which means a == b */
        if (!cmp(key, current_key) && !cmp(current_key, key)) {
            memtable::MemtableValue value{current_value,
                                          static_cast<bool>(tombstone)};
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
