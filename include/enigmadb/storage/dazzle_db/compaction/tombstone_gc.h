/**
 * @file compaction.h
 * @brief Common compaction implementation
 *
 * @author frostzt
 * @date 2026-08-02
 */

#ifndef ENIGMA_DB_TOMBSTONE_GC_H
#define ENIGMA_DB_TOMBSTONE_GC_H

#include <vector>

#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"

namespace enigmadb::dazzle {

/* Returns true if the inputs are valid and exist in the provided live array.
 * NOTE: live MUST be sorted */
bool can_drop_tombstones(const std::vector<SSTableId>& live,
                         const std::vector<SSTableId>& inputs);

}  // namespace enigmadb::dazzle

#endif  // ENIGMA_DB_TOMBSTONE_GC_H
