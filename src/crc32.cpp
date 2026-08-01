#include "enigmadb/crc32.h"

namespace enigmadb {

uint32_t compute_crc_32(const uint8_t* data, const size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i) {
        const auto byte = static_cast<uint8_t>(data[i]);
        crc = (crc >> 8) ^ crc_32_table[(crc ^ byte) & 0xFF];
    }
    return ~crc;
}

}  // namespace enigmadb
