#include "enigmadb/common/crc32.h"

#include <gtest/gtest.h>

using namespace enigmadb::common;

TEST(crc32, produces_same_checksum) {
    uint8_t buffer[] = "sourav";
    auto crc32 = compute_crc_32(buffer, 6);
    auto crc32_d = compute_crc_32(buffer, 6);
    ASSERT_EQ(crc32, crc32_d);
}

TEST(crc32, produces_different_checksum) {
    uint8_t buffer[] = "sourav";
    uint8_t buffer2[] = "gourav";
    auto crc32 = compute_crc_32(buffer, 6);
    auto crc32_d = compute_crc_32(buffer2, 6);
    ASSERT_NE(crc32, crc32_d);
}

TEST(crc32, corruption_with_byte_flipped) {
    uint8_t buffer[] = "sourav";
    auto crc32 = compute_crc_32(buffer, 6);
    buffer[2] ^= 0xFF;
    auto corrupted_crc32 = compute_crc_32(buffer, 6);
    ASSERT_NE(crc32, corrupted_crc32);
}
