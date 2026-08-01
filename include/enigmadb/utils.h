/*
 * @file utils.h
 * @brief Utilities
 * @author frostzt
 * @date 2026-04-02
 */

#ifndef ENIGMA_DB_UTILS_H
#define ENIGMA_DB_UTILS_H

#include <stdint.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

namespace enigmadb {

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

inline std::string trim_string(const std::string& s) {
    const std::string whitespace = " \t\n\r\f\v";

    const auto str_begin = s.find_first_not_of(whitespace);
    if (str_begin == std::string::npos) return "";

    const auto str_end = s.find_last_not_of(whitespace);
    const auto str_range = str_end - str_begin + 1;

    return s.substr(str_begin, str_range);
}

inline void trim_in_place(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
                return !std::isspace(ch);
            }));
    s.erase(std::find_if(s.rbegin(), s.rend(),
                         [](unsigned char ch) { return !std::isspace(ch); })
                .base(),
            s.end());
}

}  // namespace enigmadb

#endif  // ENIGMA_DB_UTILS_H
