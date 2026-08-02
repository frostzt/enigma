#include "enigmadb/storage/dazzle_db/memtable/memtable.h"

#include <cassert>
#include <cstddef>
#include <optional>
#include <vector>

#include "enigmadb/storage/dazzle_db/internal_value.h"
#include "enigmadb/storage/key.h"

namespace enigmadb::dazzle {

void Memtable::put(const storage::Key& key, std::span<const uint8_t> value,
                   uint64_t sequence) {
    InternalValue memtable_value{
        std::vector<uint8_t>(value.begin(), value.end()), false, sequence};

    size_t to_add = memtable_value.data.size();
    size_t to_remove = 0;
    if (auto exists = entries_.find(key); exists != entries_.end()) {
        to_remove = exists->second.data.size();
    } else {
        to_add += key.size(); /* this is a new key */
    }

    entries_[key] = memtable_value;
    bytes_ += to_add;
    bytes_ -= to_remove;
}

void Memtable::remove(const storage::Key& key, uint64_t sequence) {
    size_t to_remove = 0;
    size_t to_add = 0;
    if (auto exists = entries_.find(key); exists != entries_.end()) {
        to_remove = exists->second.data.size();
    } else {
        to_add += key.size(); /* a new key */
    }

    entries_[key] = InternalValue{{}, true, sequence};
    bytes_ += to_add;
    bytes_ -= to_remove;
}

std::optional<InternalValue> Memtable::get(const storage::Key& key) {
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

}  // namespace enigmadb::dazzle
