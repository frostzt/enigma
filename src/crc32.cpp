#include "enigmadb/crc32.h"

#include <cstring>

#if ENIGMADB_CRC32_X86
#include <nmmintrin.h>
#endif

namespace enigmadb {

uint32_t crc32c_scaler(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; i++) {
        const auto byte = static_cast<uint8_t>(data[i]);
        crc = (crc >> 8) ^ crc_32_table[(crc ^ byte) & 0xFF];
    }
    return ~crc;
}

#if ENIGMADB_CRC32_X86
__attribute__((target("sse4.2"))) uint32_t crc32c_hw(const uint8_t* data, size_t length) {
    uint64_t crc = 0xFFFFFFFFU;
    while (length >= 8) {
        uint64_t chunk;
        memcpy(&chunk, data, 8);
        crc = _mm_crc32_u64(crc, chunk);
        data += 8;
        length -= 8;
    }

    for (size_t i = 0; i < length; i++) {
        const auto byte = static_cast<uint8_t>(data[i]);
        crc = (crc >> 8) ^ crc_32_table[(crc ^ byte) & 0xFF];
    }

    return ~static_cast<uint32_t>(crc);
}
#endif

uint32_t compute_crc_32(const uint8_t* data, const size_t length) {
#if ENIGMADB_CRC32_X86
    static const bool hw = __builtin_cpu_supports("sse4.2");
    if (hw) return crc32c_hw(data, length);
#endif
    return crc32c_scaler(data, length);
}

}  // namespace enigmadb
