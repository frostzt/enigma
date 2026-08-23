#include "enigmadb/buffer.h"

#include <cstdint>
#include <span>
#include <vector>

#include "gtest/gtest.h"

using namespace enigmadb;

TEST(BufferWriter, emits_correct_big_endian) {
    BufferWriter w(25);
    std::vector<uint8_t> expected1 = {0x00, 0x00, 0x00, 0x01};
    w.write_u32(1);
    EXPECT_EQ(std::vector<uint8_t>(w.data().begin(), w.data().end()), expected1);

    w.clear();

    std::vector<uint8_t> expected2 = {0x01, 0x02, 0x03, 0x04};
    w.write_u32(0x01020304);
    EXPECT_EQ(std::vector<uint8_t>(w.data().begin(), w.data().end()), expected2);

    w.clear();

    std::vector<uint8_t> expected3 = {0x00, 0x00, 0x00, 0xAB};
    w.write_u32(0xAB);
    EXPECT_EQ(std::vector<uint8_t>(w.data().begin(), w.data().end()), expected3);

    w.clear();

    std::vector<uint8_t> expected4 = {0xAB};
    w.write_u8(0xAB);
    EXPECT_EQ(std::vector<uint8_t>(w.data().begin(), w.data().end()), expected4);

    w.clear();

    std::vector<uint8_t> expected5 = {0x01, 0x02};
    w.write_u16(0x0102);
    EXPECT_EQ(std::vector<uint8_t>(w.data().begin(), w.data().end()), expected5);

    w.clear();

    std::vector<uint8_t> expected6 = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    w.write_u64(0x0102030405060708);
    EXPECT_EQ(std::vector<uint8_t>(w.data().begin(), w.data().end()), expected6);
}

TEST(BufferWriter, write_bytes) {
    BufferWriter w(25);

    std::vector<uint8_t> value = {1, 2, 3, 4, 5};
    std::span<const uint8_t> vref{value};
    w.write_bytes(vref);

    EXPECT_EQ(std::vector<uint8_t>(w.data().begin(), w.data().end()), value);
}

TEST(BufferWriter, every_width_min_max) {
    BufferWriter w(50);

    /* 8 bits */
    w.write_u8(UINT8_MAX);
    EXPECT_EQ(std::vector<uint8_t>(w.data().begin(), w.data().end()), (std::vector<uint8_t>{0xFF}));
    w.clear();

    w.write_u8(0);
    EXPECT_EQ(std::vector<uint8_t>(w.data().begin(), w.data().end()), (std::vector<uint8_t>{0x00}));
    w.clear();

    /* 16 bits */
    w.write_u16(UINT16_MAX);
    EXPECT_EQ(std::vector<uint8_t>(w.data().begin(), w.data().end()), (std::vector<uint8_t>{0xFF, 0xFF}));
    w.clear();

    w.write_u16(0);
    EXPECT_EQ(std::vector<uint8_t>(w.data().begin(), w.data().end()), (std::vector<uint8_t>{0x00, 0x00}));
    w.clear();

    /* 32 bits */
    w.write_u32(UINT32_MAX);
    EXPECT_EQ(std::vector<uint8_t>(w.data().begin(), w.data().end()), (std::vector<uint8_t>{0xFF, 0xFF, 0xFF, 0xFF}));
    w.clear();

    w.write_u32(0);
    EXPECT_EQ(std::vector<uint8_t>(w.data().begin(), w.data().end()), (std::vector<uint8_t>{0x00, 0x00, 0x00, 0x00}));
    w.clear();

    /* 64 bits */
    w.write_u64(UINT64_MAX);
    EXPECT_EQ(std::vector<uint8_t>(w.data().begin(), w.data().end()),
              (std::vector<uint8_t>{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
    w.clear();

    w.write_u64(0);
    EXPECT_EQ(std::vector<uint8_t>(w.data().begin(), w.data().end()),
              (std::vector<uint8_t>{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));
    w.clear();
}

TEST(Buffer, reads_back_in_order) {
    BufferWriter w(50);

    std::vector<uint8_t> random = {0x02, 0x01, 0xAA, 0xAB, 0xFF, 0xFF, 0x02, 0x07};

    /* write a bunc of data */
    w.write_u8(121);
    w.write_u16(1345);
    w.write_bytes({random});
    w.write_u32(235626);
    w.write_u64(235626234567);

    BufferReader r(w.data().data(), w.size());

    /* read the data in the same order */
    ASSERT_EQ(r.read_u8(), 121);
    ASSERT_EQ(r.read_u16(), 1345);
    auto got = r.read_bytes(random.size());
    ASSERT_TRUE(std::equal(got.begin(), got.end(), random.begin(), random.end()));
    ASSERT_EQ(r.read_u32(), 235626);
    ASSERT_EQ(r.read_u64(), 235626234567);

    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.consumed(), w.size());
}
