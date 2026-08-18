#ifndef ENIGMADB_INCLUDE_UTILS_H_
#define ENIGMADB_INCLUDE_UTILS_H_

#include <cstdint>

[[nodiscard]] inline uint64_t mb_to_b(uint64_t mb) { return mb * 1024 * 1024; }

[[nodiscard]] inline uint64_t b_to_mb(uint64_t bytes) { return bytes >> 20; }

#endif  // ENIGMADB_INCLUDE_UTILS_H_
