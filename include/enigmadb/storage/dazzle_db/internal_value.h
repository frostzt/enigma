/**
 * @author frostzt
 * @date 2026-04-05
 */

#ifndef ENIGMA_DB_INTERNAL_VALUE_H
#define ENIGMA_DB_INTERNAL_VALUE_H

#include <stdint.h>

#include <cstdint>
#include <vector>

namespace enigmadb::dazzle {

struct InternalValue {
    /// Raw column value; empty when is_tombstone is true.
    std::vector<uint8_t> data;
    /// True if this entry represents a deletion.
    bool is_tombstone = false;
    /// Unique sequence number for this record.
    uint64_t sequence = 0;
};

}  // namespace enigmadb::dazzle

#endif  // ENIGMA_DB_INTERNAL_VALUE_H
