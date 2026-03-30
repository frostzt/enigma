#include "enigmadb/common/encoding.h"

#include <gtest/gtest.h>

TEST(encoding, round_trip_uint8) {
    uint8_t value = 12;
    char buffer[50] = "";

    auto encode_return = encode_uint8(value, buffer, 0);
    ASSERT_EQ(encode_return, 1);

    auto ret_value = decode_uint8(buffer, 0);
    ASSERT_EQ(ret_value, value);
}

TEST(encoding, round_trip_uint8_max) {
    uint8_t value = UINT8_MAX;
    char buffer[50] = "";

    auto encode_return = encode_uint8(value, buffer, 0);
    ASSERT_EQ(encode_return, 1);

    auto ret_value = decode_uint8(buffer, 0);
    ASSERT_EQ(ret_value, value);
}

TEST(encoding, round_trip_uint8_zero) {
    uint8_t value = 0;
    char buffer[50] = "";

    auto encode_return = encode_uint8(value, buffer, 0);
    ASSERT_EQ(encode_return, 1);

    auto ret_value = decode_uint8(buffer, 0);
    ASSERT_EQ(ret_value, value);
}

TEST(encoding, round_trip_uint16_max) {
    uint16_t value = UINT16_MAX;
    char buffer[50] = "";

    auto encode_return = encode_uint16(value, buffer, 0);
    ASSERT_EQ(encode_return, 2);

    auto ret_value = decode_uint16(buffer, 0);
    ASSERT_EQ(ret_value, value);
}

TEST(encoding, round_trip_uint16_zero) {
    uint16_t value = 0;
    char buffer[50] = "";

    auto encode_return = encode_uint16(value, buffer, 0);
    ASSERT_EQ(encode_return, 2);

    auto ret_value = decode_uint16(buffer, 0);
    ASSERT_EQ(ret_value, value);
}

TEST(encoding, round_trip_uint32_max) {
    uint32_t value = UINT32_MAX;
    char buffer[50] = "";

    auto encode_return = encode_uint32(value, buffer, 0);
    ASSERT_EQ(encode_return, 4);

    auto ret_value = decode_uint32(buffer, 0);
    ASSERT_EQ(ret_value, value);
}

TEST(encoding, round_trip_uint32_zero) {
    uint32_t value = 0;
    char buffer[50] = "";

    auto encode_return = encode_uint32(value, buffer, 0);
    ASSERT_EQ(encode_return, 4);

    auto ret_value = decode_uint32(buffer, 0);
    ASSERT_EQ(ret_value, value);
}

TEST(encoding, round_trip_uint64_max) {
    uint64_t value = UINT64_MAX;
    char buffer[50] = "";

    auto encode_return = encode_uint64(value, buffer, 0);
    ASSERT_EQ(encode_return, 8);

    auto ret_value = decode_uint64(buffer, 0);
    ASSERT_EQ(ret_value, value);
}

TEST(encoding, round_trip_uint64_zero) {
    uint64_t value = 0;
    char buffer[50] = "";

    auto encode_return = encode_uint64(value, buffer, 0);
    ASSERT_EQ(encode_return, 8);

    auto ret_value = decode_uint64(buffer, 0);
    ASSERT_EQ(ret_value, value);
}

TEST(encoding, round_trip_arbitrary) {
    char buffer[256] = "";

    // 8 bits
    auto byte = encode_uint8(UINT8_MAX, buffer, 0);
    ASSERT_EQ(byte, 1);

    // 16 bits
    auto two_bytes = encode_uint16(UINT16_MAX, buffer, 12);
    ASSERT_EQ(two_bytes, 14);

    // 32 bits
    auto four_bytes = encode_uint32(UINT32_MAX, buffer, 30);
    ASSERT_EQ(four_bytes, 34);

    // 64 bits
    auto eight_bytes = encode_uint64(UINT64_MAX, buffer, 80);
    ASSERT_EQ(eight_bytes, 88);

    auto byte_value = decode_uint8(buffer, 0);
    ASSERT_EQ(byte_value, UINT8_MAX);

    auto two_byte_value = decode_uint16(buffer, 12);
    ASSERT_EQ(two_byte_value, UINT16_MAX);

    auto four_byte_value = decode_uint32(buffer, 30);
    ASSERT_EQ(four_byte_value, UINT32_MAX);

    auto eight_byte_value = decode_uint64(buffer, 80);
    ASSERT_EQ(eight_byte_value, UINT64_MAX);
}
