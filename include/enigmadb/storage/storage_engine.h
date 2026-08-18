/**
 * @file storage_engine.h
 *
 * @author frostzt
 * @date 2026-04-05
 */

#ifndef ENIGMA_DB_STORAGE_ENGINE_H
#define ENIGMA_DB_STORAGE_ENGINE_H

#include <optional>
#include <span>

#include "enigmadb/base.h"
#include "enigmadb/storage/key.h"
#include "enigmadb/storage/value.h"

namespace enigmadb::storage {

class StorageEngine {
   public:
    virtual ~StorageEngine() = default;

    StorageEngine(const StorageEngine&) = delete;
    StorageEngine& operator=(const StorageEngine&) = delete;

    virtual Result<void> put(const Key& key, std::span<const uint8_t> value) = 0;
    virtual Result<void> remove(const Key& key) = 0;
    virtual Result<std::optional<Value>> get(const Key& key) = 0;

    /* TODO: This needs to be implemented */
    // virtual Result<std::unique_ptr<Iterator>> scan(const KeyRange& range) =
    // 0;

   protected:
    StorageEngine() = default;
};

}  // namespace enigmadb::storage

#endif  // ENIGMA_DB_STORAGE_ENGINE_H
