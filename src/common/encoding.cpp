#include "enigmadb/common/encoding.h"

#include <cstddef>
#include <cstring>

namespace enigmadb::common {

size_t encode_uint8(uint8_t value, uint8_t* buffer, size_t offset) {
    buffer[offset] = value;
    return offset + 1;
}

uint8_t decode_uint8(const uint8_t* buffer, size_t offset) {
    return static_cast<uint8_t>(buffer[offset]);
}

size_t encode_uint16(uint16_t value, uint8_t* buffer, size_t offset) {
    for (size_t i = 0; i < 2; i++) {
        buffer[offset + i] = static_cast<uint8_t>(value >> ((1 - i) * 8));
    }
    return offset + 2;
}

uint16_t decode_uint16(const uint8_t* buffer, size_t offset) {
    return (static_cast<uint16_t>(static_cast<uint8_t>(buffer[offset])) << 8) |
           static_cast<uint16_t>(static_cast<uint8_t>(buffer[offset + 1]));
}

size_t encode_uint32(uint32_t value, uint8_t* buffer, size_t offset) {
    for (size_t i = 0; i < 4; i++) {
        buffer[offset + i] = static_cast<uint8_t>(value >> ((3 - i) * 8));
    }
    return offset + 4;
}

uint32_t decode_uint32(const uint8_t* buffer, size_t offset) {
    uint32_t total = 0;
    for (size_t i = 0; i < 4; i++) {
        total |= static_cast<uint32_t>(static_cast<uint8_t>(buffer[offset + i]))
                 << ((3 - i) * 8);
    }
    return total;
}

size_t encode_uint64(uint64_t value, uint8_t* buffer, size_t offset) {
    for (size_t i = 0; i < 8; i++) {
        buffer[offset + i] = static_cast<uint8_t>(value >> ((7 - i) * 8));
    }
    return offset + 8;
}

uint64_t decode_uint64(const uint8_t* buffer, size_t offset) {
    uint64_t total = 0;
    for (size_t i = 0; i < 8; i++) {
        total |= static_cast<uint64_t>(static_cast<uint8_t>(buffer[offset + i]))
                 << ((7 - i) * 8);
    }
    return total;
}

size_t encode_bytes(const void* data, size_t length, uint8_t* buffer,
                    size_t offset) {
    memcpy(buffer + offset, data, length);
    return offset + length;
}

}  // namespace enigmadb::common
