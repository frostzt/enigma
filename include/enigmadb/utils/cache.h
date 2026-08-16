#ifndef ENIGMADB_UTILS_INCLUDE_CACHE_H_
#define ENIGMADB_UTILS_INCLUDE_CACHE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace enigmadb::utils {

class Cache {
   public:
    Cache() = default;

    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    virtual ~Cache() = default;

    /**
     * Handle for entry stored in the cache this will be returned and then released
     */
    struct Handle {};

    /**
     * Inserts a kv pair into the cache and gives it a charge against total capacity.
     *
     * When the entry is no longer needed it it passed to the deleter
     */
    virtual Handle* insert(const std::string_view key, void* value, size_t charge,
                           void (*deleter)(const std::string_view key, void* value)) = 0;

    /**
     * Returns the mapping for they provided `key` caller MUST call this->release(handle)
     */
    virtual Handle* lookup(const std::string_view key) = 0;

    /**
     * Releases the mapping returned by lookup
     */
    virtual void release(Handle* handle) = 0;

    /**
     * Returns the value encapsulated in a handle returned by a successful lookup
     */
    virtual void* value(Handle* handle) = 0;

    /**
     * Erase deletes a key from cache if it exists
     */
    virtual void erase(const std::string_view key) = 0;

    virtual uint64_t new_id() = 0;

    /* Removes keys from the cache that are NOT actively being used */
    virtual void prune() {};

    /* Returns an estimate of total capacity consumed (charge) by the entries within the cache */
    virtual size_t total_charge() const = 0;
};

std::unique_ptr<Cache> NewLRUCache(size_t capacity, size_t num_shards);

}  // namespace enigmadb::utils

#endif  // ENIGMADB_UTILS_INCLUDE_CACHE_H_
