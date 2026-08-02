#include "enigmadb/hash.h"

#include "enigmadb/encoding.h"

namespace enigmadb {

uint32_t Hash(const uint8_t* data, size_t n, uint32_t seed) {
    // Similar to murmur hash
    const uint32_t m = 0xc6a4a793;
    const uint32_t r = 24;
    const uint8_t* limit = data + n;
    uint32_t h = seed ^ (n * m);

    // Pick up four bytes at a time
    while (limit - data >= 4) {
        uint32_t w = decode_uint32(data, 0);
        data += 4;
        h += w;
        h *= m;
        h ^= (h >> 16);
    }

    // Pick up remaining bytes
    switch (limit - data) {
        case 3:
            h += static_cast<uint8_t>(data[2]) << 16;
            [[fallthrough]];
        case 2:
            h += static_cast<uint8_t>(data[1]) << 8;
            [[fallthrough]];
        case 1:
            h += static_cast<uint8_t>(data[0]);
            h *= m;
            h ^= (h >> r);
            break;
    }
    return h;
}

}  // namespace enigmadb
