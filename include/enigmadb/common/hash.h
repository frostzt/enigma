
#ifndef ENIGMA_DB_HASH_H
#define ENIGMA_DB_HASH_H

#include <cstddef>
#include <cstdint>

namespace enigmadb::common {

uint32_t Hash(const uint8_t* data, size_t n, uint32_t seed);

}  // namespace enigmadb::common

#endif  // ENIGMA_DB_HASH_H
