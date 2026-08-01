#ifndef ENIGMA_DB_KEY_H
#define ENIGMA_DB_KEY_H

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace enigmadb::storage {

class Key {
   public:
    Key() = default;
    explicit Key(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
    std::span<const uint8_t> bytes() const { return bytes_; }

    bool operator==(const Key&) const = default;
    auto operator<=>(const Key& b) const = default;

   private:
    std::vector<uint8_t> bytes_;
};

}  // namespace enigmadb::storage

#endif  // ENIGMA_DB_KEY_H
