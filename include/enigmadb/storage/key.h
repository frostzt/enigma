#ifndef ENIGMA_DB_KEY_H
#define ENIGMA_DB_KEY_H

#include <cstdint>
#include <iterator>
#include <span>
#include <utility>
#include <vector>

namespace enigmadb::storage {

template <typename It>
concept ContiguousByteIterator = std::contiguous_iterator<It> && std::same_as<std::iter_value_t<It>, uint8_t>;

class Key {
   public:
    Key() = default;
    explicit Key(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
    std::span<const uint8_t> bytes() const { return bytes_; }
    size_t size() const { return bytes_.size(); };

    template <ContiguousByteIterator It>
    void assign(It f, It l) {
        bytes_.assign(f, l);
    }

    bool operator==(const Key&) const = default;
    auto operator<=>(const Key& b) const = default;

   private:
    std::vector<uint8_t> bytes_;
};

}  // namespace enigmadb::storage

#endif  // ENIGMA_DB_KEY_H
