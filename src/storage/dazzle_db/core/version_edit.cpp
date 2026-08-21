#include "enigmadb/storage/dazzle_db/core/version_edit.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "enigmadb/base.h"
#include "enigmadb/crc32.h"
#include "enigmadb/encoding.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"

namespace enigmadb::dazzle {

/**
 * --- HEADER ---
 * Length (4 bytes)
 * CRC32  (4 bytes)
 *
 * --- BODY ---
 * Removed Length   (4 bytes)
 * ---> SSTableId   x removed
 * Added Table Meta (4 bytes)
 * ---> SSTableMeta x added
 * Has SST ID       (1 byte)
 * Next SST ID      (8 bytes)
 */
std::vector<uint8_t> serialize_version_edit(const VersionEdit& ve) {
    auto to_alloc = get_version_edit_record_size(ve);

    std::vector<uint8_t> out(to_alloc);
    auto buf = out.data();

    size_t offset = 0;

    /* --- write header --- */
    offset = encode_uint32(0, buf, offset);
    offset = encode_uint32(0, buf, offset);

    /* --- write body --- */
    offset = encode_uint32(static_cast<uint32_t>(ve.removed.size()), buf, offset); /* removed length */
    for (const auto& r : ve.removed) {
        offset = encode_uint64(r.value, buf, offset);
    }

    offset = encode_uint32(static_cast<uint32_t>(ve.added.size()), buf, offset); /* added length */
    for (const auto& a : ve.added) {
        offset = encode_uint64(a.id.value, buf, offset);
        offset = encode_uint64(a.size_bytes, buf, offset);
        offset = encode_uint64(a.entry_count, buf, offset);
        offset = encode_uint64(a.max_sequence, buf, offset);
    }

    if (ve.next_sst_id.has_value()) {
        offset = encode_uint8(1, buf, offset);
        offset = encode_uint64(ve.next_sst_id.value(), buf, offset);
    } else {
        offset = encode_uint8(0, buf, offset);
    }

    auto body_length = offset - 8; /* sub the header */
    auto checksum = compute_crc_32(buf + 8, body_length);
    encode_uint32(body_length, buf, 0);
    encode_uint32(checksum, buf, 4);

    return out;
}

Result<size_t> deserialize_version_edit(const uint8_t* buffer, size_t length, VersionEdit& edit) {
    VersionEdit ve;
    size_t offset = 0;

    constexpr size_t MIN_RECORD_SIZE = /* header */ 8 + /* lengths */ 8 + /* sstid flag */ 1;
    if (length < MIN_RECORD_SIZE) {
        return Result<size_t>::err(Error::incomplete_record("buffer too small for version edit"));
    }

    /* --- read header --- */
    auto body_length = decode_uint32(buffer, offset);
    offset += 4;
    auto checksum = decode_uint32(buffer, offset);
    offset += 4;

    if (body_length > length - 8) {
        return Result<size_t>::err(Error::incomplete_record("the record body is incomplete"));
    }

    auto record_boundry = body_length + 8;

    /* validate checksum */
    auto gen_checksum = compute_crc_32(buffer + 8, body_length);
    if (checksum != gen_checksum) {
        return Result<size_t>::err(Error::checksum_mismatch("checksum mismatch - corrupted data found"));
    }

    /* --- read removed sstable ids --- */
    if (offset + 4 > record_boundry) {
        return Result<size_t>::err(Error::corruption("corrupt record expected body to contain more data"));
    }

    auto removed_count = decode_uint32(buffer, offset);
    offset += 4;

    size_t max_plausible_removed = (record_boundry - offset) / 8;
    if (removed_count > max_plausible_removed) {
        return Result<size_t>::err(Error::corruption("removed count exceeds the max boundry"));
    }

    ve.removed.reserve(removed_count);
    for (size_t i = 0; i < removed_count; i++) {
        ve.removed.push_back(SSTableId{decode_uint64(buffer, offset)});
        offset += 8;
    }

    /* --- read added sstable metas --- */
    if (offset + 4 > record_boundry) {
        return Result<size_t>::err(Error::corruption("corrupt record expected body to contain more data"));
    }

    auto added_count = decode_uint32(buffer, offset);
    offset += 4;

    size_t max_plausible_added = (record_boundry - offset) / SSTABLE_META_SIZE;
    if (added_count > max_plausible_added) {
        return Result<size_t>::err(Error::corruption("added count exceeds the max boundry"));
    }

    ve.added.reserve(added_count);
    for (size_t i = 0; i < added_count; i++) {
        auto id = SSTableId{decode_uint64(buffer, offset)};
        offset += 8;
        auto size_bytes = decode_uint64(buffer, offset);
        offset += 8;
        auto entry_count = decode_uint64(buffer, offset);
        offset += 8;
        auto max_sequence = decode_uint64(buffer, offset);
        offset += 8;
        ve.added.push_back(
            SSTableMeta{.id = id, .size_bytes = size_bytes, .entry_count = entry_count, .max_sequence = max_sequence});
    }

    /* --- read next sstable id --- */
    if (offset + 1 > record_boundry) {
        return Result<size_t>::err(Error::corruption("corrupt record expected body to contain more data"));
    }

    auto has_id = decode_uint8(buffer, offset);
    offset += 1;
    if (has_id == 1) {
        if (offset + 8 > record_boundry) {
            return Result<size_t>::err(Error::corruption("corrupt record expected body to contain more data"));
        }
        ve.next_sst_id = decode_uint64(buffer, offset);
        offset += 8;
    }

    if (offset != record_boundry) {
        return Result<size_t>::err(Error::corruption("corrupt record expected body to contain more data"));
    }

    // move the local edit to ve
    edit = std::move(ve);
    return Result<size_t>::ok(offset);
}

}  // namespace enigmadb::dazzle
