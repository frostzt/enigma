#ifndef ENIGMA_DB_ENGINE_REGISTRY_H
#define ENIGMA_DB_ENGINE_REGISTRY_H

#include <functional>
#include <map>
#include <memory>
#include <string_view>
#include <unordered_map>

#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/storage_engine.h"

namespace enigmadb::storage {

using EngineConfig = std::map<std::string, std::string>;

using EngineFactory = std::function<Result<std::unique_ptr<StorageEngine>>(io::IOEngine&, const EngineConfig&)>;

class EngineRegistry {
   public:
    static EngineRegistry& instance();

    void register_engine(std::string name, EngineFactory factory);

    Result<std::unique_ptr<StorageEngine>> create(std::string_view, io::IOEngine&, const EngineConfig&) const;

   private:
    std::unordered_map<std::string, EngineFactory> factories_;
};

};  // namespace enigmadb::storage

#endif  // ENIGMA_DB_KEY_RANGE_H
