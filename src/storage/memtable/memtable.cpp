#include "enigmadb/storage/memtable/memtable.h"

#include <cassert>
#include <cstddef>
#include <optional>

#include "enigmadb/storage/key_encoding.h"

namespace enigmadb::storage::memtable {

void Memtable::put(const std::vector<uint8_t>& partition_key,
                   const std::vector<uint8_t>& clustering_key,
                   const std::string& column_name,
                   const std::vector<uint8_t>& value, uint64_t sequence) {
    auto key = encode_composite_key(partition_key, clustering_key, column_name);
    MemtableValue memtable_value{value, false, sequence};

    size_t to_remove = 0;
    if (auto exists = entries_.find(key); exists != entries_.end()) {
        to_remove = exists->second.data.size();
    }

    entries_[key] = memtable_value;
    size_t to_add = key.size() + memtable_value.data.size();
    bytes_ += (to_add - to_remove);
}

void Memtable::remove(const std::vector<uint8_t>& partition_key,
                      const std::vector<uint8_t>& clustering_key,
                      const std::string& column_name, uint64_t sequence) {
    auto key = encode_composite_key(partition_key, clustering_key, column_name);
    size_t to_remove = 0;
    if (auto exists = entries_.find(key); exists != entries_.end()) {
        to_remove = exists->second.data.size();
    }

    entries_[key] = MemtableValue{{}, true, sequence};
    bytes_ += key.size() - to_remove;
}

std::optional<MemtableValue> Memtable::get(
    const std::vector<uint8_t>& partition_key,
    const std::vector<uint8_t>& clustering_key,
    const std::string& column_name) {
    auto key = encode_composite_key(partition_key, clustering_key, column_name);
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return it->second;
}

size_t Memtable::approximate_size() const { return bytes_; };

bool Memtable::should_flush() const {
    if (bytes_ >= max_memtable_size_) {
        return true;
    }
    return false;
};

}  // namespace enigmadb::storage::memtable
