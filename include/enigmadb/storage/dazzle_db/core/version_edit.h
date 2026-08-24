#ifndef ENIGMADB_DAZZLEDB_CORE_VERSION_EDIT_H_
#define ENIGMADB_DAZZLEDB_CORE_VERSION_EDIT_H_

#include <optional>
#include <vector>

#include "enigmadb/base.h"
#include "enigmadb/buffer.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"

namespace enigmadb::dazzle {

struct VersionEdit {
    std::vector<SSTableId> removed;
    std::vector<SSTableMeta> added;
    std::optional<uint64_t> next_sst_id{};
};

/// Encodes version edit into the buffer writer
void encode_version_edit(BufferWriter&, const VersionEdit&);

/// Decodes a possible version edit buffer into the buffer reader, the function
/// validates the body reader in read_framed as the entire thing is wrapped in
/// a frame its much better to call this from read_frame
[[nodiscard]] Result<VersionEdit> decode_version_edit(BufferReader&);

}  // namespace enigmadb::dazzle

#endif  // ENIGMADB_DAZZLEDB_CORE_VERSION_EDIT_H_
