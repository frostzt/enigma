#include "enigmadb/storage/dazzle_db/compaction/tombstone_gc.h"

#include <algorithm>
#include <cassert>
#include <vector>

#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"

namespace enigmadb::dazzle {

bool can_drop_tombstones(const std::vector<SSTableId>& live,
                         const std::vector<SSTableId>& inputs) {
    assert(std::is_sorted(live.begin(), live.end()));

    if (inputs.size() == 0 || live.size() < inputs.size()) {
        return false;
    }

    std::vector<SSTableId> inputs_copy = inputs;
    std::sort(inputs_copy.begin(), inputs_copy.end());

    for (size_t i = 0; i < inputs_copy.size(); i++) {
        if (live[i] != inputs_copy[i]) return false;
    }

    return true;
}

}  // namespace enigmadb::dazzle
