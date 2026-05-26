/*
 * @file utils.h
 * @brief Utilities
 * @author frostzt
 * @date 2026-04-02
 */

#ifndef ENIGMA_DB_UTILS_H
#define ENIGMA_DB_UTILS_H

#include <stdint.h>

#include <filesystem>
#include <vector>

namespace enigmadb::common {

inline std::vector<uint8_t> string_to_bytes(std::string_view str) {
    return {str.begin(), str.end()};
}

inline std::string bytes_to_string(const std::vector<uint8_t>& vec) {
    return std::string(vec.begin(), vec.end());
}

inline void clear_dir(const std::filesystem::path& p) {
    for (const auto& entry : std::filesystem::directory_iterator(p)) {
        std::filesystem::remove_all(entry.path());
    }
}

}  // namespace enigmadb::common

#endif  // ENIGMA_DB_UTILS_H
