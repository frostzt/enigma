#include "enigmadb/storage/dazzle_db/core/version_edit.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "enigmadb/base.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"

namespace enigmadb::dazzle {

void encode_version_edit(BufferWriter& bw, const VersionEdit& ve) {
    /* --- write sstable ids removed --- */
    bw.write_u32(static_cast<uint32_t>(ve.removed.size()));
    for (const auto& r : ve.removed) {
        bw.write_u64(r.value);
    }

    /* --- write sstables meta added --- */
    bw.write_u32(static_cast<uint32_t>(ve.added.size())); /* added len */
    for (const auto& a : ve.added) {
        bw.write_u64(a.id.value);
        bw.write_u64(a.size_bytes);
        bw.write_u64(a.entry_count);
        bw.write_u64(a.max_sequence);
    }

    /* --- write next sstable id if available --- */
    if (ve.next_sst_id.has_value()) {
        bw.write_u8(1);
        bw.write_u64(ve.next_sst_id.value());
    } else {
        bw.write_u8(0);
    }
}

Result<VersionEdit> decode_version_edit(BufferReader& br) {
    VersionEdit local;

    /* --- read removed ids --- */
    auto removed_count = br.read_u32();
    if (removed_count > br.remaining() / 8)
        return Result<VersionEdit>::err(Error::corruption("removed count exceeds the max boundry"));
    local.removed.reserve(removed_count);
    for (size_t i = 0; i < removed_count; i++) {
        local.removed.push_back(SSTableId{br.read_u64()});
    }

    /* --- read added sst metas --- */
    auto added_count = br.read_u32();
    if (added_count > br.remaining() / SSTABLE_META_SIZE) {
        return Result<VersionEdit>::err(Error::corruption("added count exceeds the max boundry"));
    }
    local.added.reserve(added_count);
    for (size_t i = 0; i < added_count; i++) {
        auto id = SSTableId{br.read_u64()};
        auto szbytes = br.read_u64();
        auto ecount = br.read_u64();
        auto max_seq = br.read_u64();
        local.added.push_back(
            SSTableMeta{.id = id, .size_bytes = szbytes, .entry_count = ecount, .max_sequence = max_seq});
    }

    /* --- read next sstable id --- */
    auto has_id = br.read_u8();
    if (has_id == 1) {
        local.next_sst_id = br.read_u64();
    }

    if (!br.ok()) return Result<VersionEdit>::err(br.error());
    return Result<VersionEdit>::ok(std::move(local));
}

}  // namespace enigmadb::dazzle
