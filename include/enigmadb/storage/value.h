#ifndef ENIGMA_DB_VALUE_H
#define ENIGMA_DB_VALUE_H

#include <cstdint>
#include <vector>

namespace enigmadb::storage {

class Value {
   public:
    std::vector<uint8_t> data;
};

}  // namespace enigmadb::storage

#endif  // ENIGMA_DB_VALUE_H
