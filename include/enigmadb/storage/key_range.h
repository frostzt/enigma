#ifndef ENIGMA_DB_KEY_RANGE_H
#define ENIGMA_DB_KEY_RANGE_H

#include <optional>

#include "enigmadb/storage/key.h"

namespace enigmadb::storage {

struct KeyRange {
    std::optional<Key> start;
    std::optional<Key> end;
    bool start_inclusive = true;
    bool end_inclusive = false;
};

};  // namespace enigmadb::storage

#endif  // ENIGMA_DB_KEY_RANGE_H
