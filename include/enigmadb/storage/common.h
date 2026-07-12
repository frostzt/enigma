#ifndef ENIGMA_DB_COMMON_H
#define ENIGMA_DB_COMMON_H

#include <cstdint>
#include <sstream>
#include <string>

#include "enigmadb/storage/sstable/sstable_common.h"

namespace enigmadb::storage {

inline std::string sst_path(std::string data_dir, uint64_t seq) {
    std::stringstream ss;
    ss << data_dir << "/sst/" << sstable_filename(sstable::SSTableId{seq});
    return ss.str();
}

}  // namespace enigmadb::storage

#endif  // ENIGMA_DB_COMMON_H
