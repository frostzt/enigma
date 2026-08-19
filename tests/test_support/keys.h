#ifndef ENIGMADB_TEST_SUPPORT_KEYS_H
#define ENIGMADB_TEST_SUPPORT_KEYS_H

#include <initializer_list>
#include <string_view>
#include <vector>

#include "enigmadb/catalog/key_encoding.h"
#include "enigmadb/storage/key.h"
#include "enigmadb/utils.h"

using namespace enigmadb;

namespace enigmadb::TESTNAMESPACE {

[[nodiscard]] inline storage::Key make_key(std::string_view pk, std::string_view ck, std::string_view col) {
    return storage::Key{catalog::encode_composite_key(as_bytes(pk), as_bytes(ck), as_bytes(col))};
}

[[nodiscard]] inline std::vector<uint8_t> make_bytes(std::initializer_list<uint8_t> bytes) {
    return std::vector<uint8_t>(bytes);
}

}  // namespace enigmadb::TESTNAMESPACE

#endif
