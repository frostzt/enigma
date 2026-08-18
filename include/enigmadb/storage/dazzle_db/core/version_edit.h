#ifndef ENIGMADB_DAZZLEDB_CORE_VERSION_EDIT_H
#define ENIGMADB_DAZZLEDB_CORE_VERSION_EDIT_H

#include <optional>
#include <vector>

#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"

namespace enigmadb::dazzle {

struct VersionEdit {
    std::vector<SSTableId> removed;
    std::vector<SSTableMeta> added;
    std::optional<uint64_t> next_sst_id{};
};

}  // namespace enigmadb::dazzle

#endif  // ENIGMADB_DAZZLEDB_CORE_VERSION_EDIT_H
