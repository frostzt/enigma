#ifndef ENIGMA_DB_DAZZLE_VERSION_H
#define ENIGMA_DB_DAZZLE_VERSION_H

#include <map>
#include <memory>
#include <optional>

#include "enigmadb/base.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_reader.h"
#include "enigmadb/storage/key.h"
#include "enigmadb/storage/value.h"

namespace enigmadb::dazzle {

class Version {
   public:
    std::map<SSTableId, std::shared_ptr<SSTableReader>, SSTableIdComparator> sst_readers;
    std::map<SSTableId, std::shared_ptr<SSTableMeta>, SSTableIdComparator> sst_meta;

    Version() = default;
    ~Version() = default;

    Result<std::optional<storage::Value>> lookup(const storage::Key& key) const;
    Result<std::optional<InternalValue>> lookup_internal(const storage::Key& key) const;
};

}  // namespace enigmadb::dazzle

#endif  // ENIGMA_DB_DAZZLE_VERSION_H
