#ifndef ENIGMADB_DAZZLEDB_CORE_VERSION_EDIT_H_
#define ENIGMADB_DAZZLEDB_CORE_VERSION_EDIT_H_

#include <optional>
#include <vector>

#include "enigmadb/base.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"

namespace enigmadb::dazzle {

struct VersionEdit {
    std::vector<SSTableId> removed;
    std::vector<SSTableMeta> added;
    std::optional<uint64_t> next_sst_id{};
};

[[nodiscard]] inline size_t get_version_edit_record_size(const VersionEdit& ve) {
    size_t total_size =
        /* len */ 4 + /* checksum */ 4 + /* removed len */ 4 + /* added len */ 4 + /* next sst id flag */ 1;
    total_size += /* sstid size */ 8 * ve.removed.size();
    total_size += /* sstmeta size */ SSTABLE_META_SIZE * ve.added.size();
    if (ve.next_sst_id.has_value()) {
        total_size += /* next sst id size */ 8;
    }
    return total_size;
}

/// Serializes VersionEdit to write ready binary
[[nodiscard]] std::vector<uint8_t> serialize_version_edit(const VersionEdit& ve);

/// Deserializes VersionEdit from binary
[[nodiscard]] Result<size_t> deserialize_version_edit(const uint8_t* buffer, size_t length, VersionEdit& ve);

}  // namespace enigmadb::dazzle

#endif  // ENIGMADB_DAZZLEDB_CORE_VERSION_EDIT_H_
