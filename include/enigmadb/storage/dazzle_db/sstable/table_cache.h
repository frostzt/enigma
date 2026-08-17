#ifndef ENIGMADB_DAZZLEDB_INCLUDE_SSTABLE_TABLECACHE_H_
#define ENIGMADB_DAZZLEDB_INCLUDE_SSTABLE_TABLECACHE_H_

#include <memory>
#include <string>

#include "enigmadb/base.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_reader.h"
#include "enigmadb/utils/cache.h"

namespace enigmadb::dazzle {

class TableCache {
   public:
    TableCache(const TableCache&) = delete;
    TableCache& operator=(const TableCache&) = delete;
    TableCache(TableCache&&) = delete;
    TableCache& operator=(TableCache&&) = delete;

    /// Get an sstable from cache opens a new on miss
    Result<std::shared_ptr<SSTableReader>> get(SSTableId id);

    /// Evicts the sstable for the provided id
    void evict(SSTableId id);

    /// Creates a new instance of TableCache
    static Result<std::unique_ptr<TableCache>> create(io::IOEngine& engine, const std::string& data_dir,
                                                      std::shared_ptr<utils::Cache> cache);

   private:
    /// IO Engine handles the io required for disk access
    io::IOEngine& engine_;

    /// Holds all the active sstables in memory
    std::shared_ptr<utils::Cache> cache_;

    /// Data directory
    const std::string data_dir_;

    TableCache(io::IOEngine& engine, std::shared_ptr<utils::Cache> cache, std::string data_dir)
        : engine_(engine), cache_(std::move(cache)), data_dir_(data_dir) {}

    Result<std::shared_ptr<SSTableReader>> make_reader_return(utils::Cache::Handle* h);
};

}  // namespace enigmadb::dazzle

#endif  // ENIGMADB_DAZZLEDB_INCLUDE_SSTABLE_TABLECACHE_H_
