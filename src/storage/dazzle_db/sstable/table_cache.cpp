#include "enigmadb/storage/dazzle_db/sstable/table_cache.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>

#include "enigmadb/base.h"
#include "enigmadb/encoding.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_reader.h"
#include "enigmadb/utils.h"
#include "enigmadb/utils/cache.h"

namespace enigmadb::dazzle {

static std::string_view encode_key(SSTableId id, uint8_t (&buf)[sizeof(uint64_t)]) {
    /* construct the key */
    encode_uint64(id.value, buf, 0);
    std::string_view key(reinterpret_cast<const char*>(buf), sizeof(buf));
    return key;
}

static void delete_reader(std::string_view, void* v) { delete static_cast<SSTableReader*>(v); }

Result<std::shared_ptr<SSTableReader>> TableCache::make_reader_return(utils::Cache::Handle* h) {
    return Result<std::shared_ptr<SSTableReader>>::ok(std::shared_ptr<SSTableReader>(
        static_cast<SSTableReader*>(cache_->value(h)), [cache = cache_, h](SSTableReader*) { cache->release(h); }));
}

Result<std::unique_ptr<TableCache>> TableCache::create(io::IOEngine& engine, const std::string& data_dir,
                                                       std::shared_ptr<utils::Cache> cache) {
    if (trim_string(data_dir) == "") {
        return Result<std::unique_ptr<TableCache>>::err(Error::bad_config("Invalid data directory."));
    }
    auto table_cache = std::unique_ptr<TableCache>(new TableCache(engine, std::move(cache), data_dir));
    return Result<std::unique_ptr<TableCache>>::ok(std::move(table_cache));
}

Result<std::shared_ptr<SSTableReader>> TableCache::get(SSTableId id) {
    /* construct the key */
    uint8_t buf[sizeof(id.value)];
    auto key = encode_key(id, buf);

    /* lookup the key in cache */
    auto h = cache_->lookup(key);
    if (h != nullptr) {
        /* HIT: return */
        return make_reader_return(h);
    }

    /* MISS: create a new reader and insert into cache
     * NOTE: If two threads were to miss both will open this file and write it */
    auto r = SSTableReader::create(engine_, sst_path(data_dir_, id.value));
    if (!r.has_value()) return Result<std::shared_ptr<SSTableReader>>::err(r.error());

    auto* owned = new SSTableReader(std::move(r.value()));
    auto mem_consumed = owned->approximate_memory_usage();
    auto* handle = cache_->insert(key, owned, mem_consumed, &delete_reader);

    return make_reader_return(handle);
}

/* stops serving this sstable from cache, this doesn't mean if any reader holds it would be destroyed too */
void TableCache::evict(SSTableId id) {
    /* construct the key */
    uint8_t buf[sizeof(id.value)];

    auto key = encode_key(id, buf);

    cache_->erase(key);
}

}  // namespace enigmadb::dazzle
