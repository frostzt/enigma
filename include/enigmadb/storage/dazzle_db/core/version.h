#ifndef ENIGMA_DB_DAZZLE_VERSION_H
#define ENIGMA_DB_DAZZLE_VERSION_H

#include <map>
#include <optional>

#include "enigmadb/base.h"
#include "enigmadb/storage/dazzle_db/internal_value.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"
#include "enigmadb/storage/dazzle_db/sstable/table_cache.h"
#include "enigmadb/storage/key.h"
#include "enigmadb/storage/value.h"

namespace enigmadb::dazzle {

class Version {
   public:
    Version() = default;
    ~Version() = default;

    Result<std::optional<storage::Value>> lookup(const storage::Key& key, TableCache& cache) const;
    Result<std::optional<InternalValue>> lookup_internal(const storage::Key& key, TableCache& cache) const;

    std::vector<SSTableMeta> sst_meta_to_vector() const {
        std::vector<SSTableMeta> metas;
        for (const auto& [id, meta] : sst_meta) {
            metas.push_back(meta);
        }
        return metas;
    }

    const std::map<SSTableId, SSTableMeta, SSTableIdComparator>& files() const { return sst_meta; }

   private:
    std::map<SSTableId, SSTableMeta, SSTableIdComparator> sst_meta;
};

}  // namespace enigmadb::dazzle

#endif  // ENIGMA_DB_DAZZLE_VERSION_H
