/*
 * @file utils.h
 * @brief Utilities
 * @author frostzt
 * @date 2026-04-02
 */

#ifndef ENIGMA_DB_UTILS_H
#define ENIGMA_DB_UTILS_H

#include <vector>

namespace enigmadb::common {

inline std::vector<uint8_t> string_to_bytes(std::string_view str) {
    return {str.begin(), str.end()};
}

}  // namespace enigmadb::common

#endif  // ENIGMA_DB_UTILS_H
