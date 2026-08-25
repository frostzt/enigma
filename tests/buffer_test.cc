#include "enigmadb/buffer.h"

#include <cstdint>
#include <span>
#include <vector>

#include "gtest/gtest.h"
#include "test_support/macros.h"

using namespace enigmadb;

/* --------------------------------
 * BUFFER
 * -------------------------------- */

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

/* --------------------------------
 * BUFFER READER
 * -------------------------------- */

TEST(BufferReader, read_past_ends) {
    BufferWriter bw(16);
    bw.write_u64(444);
    bw.write_u64(666);

    BufferReader br(bw.data().data(), bw.size());
    br.skip(16); /* end */

    ASSERT_TRUE(br.ok());

    br.skip(1); /* +1 the end */

    ASSERT_FALSE(br.ok());
    ASSERT_TRUE(br.error().is_out_of_range());
}

TEST(BufferReader, poisoned_reader_checks) {
    BufferWriter bw(24);

    bw.write_u64(8);
    bw.write_u64(8);
    bw.write_u64(8);

    BufferReader br(bw.data().data(), bw.size());
    br.skip((8 * 3) + 1); /* poison */

    ASSERT_EQ(br.consumed(), 0);

    ASSERT_FALSE(br.ok());

    auto pv1 = br.read_u64();
    ASSERT_FALSE(br.ok());
    ASSERT_EQ(pv1, 0);
    ASSERT_EQ(br.consumed(), 0);
}

TEST(BufferReader, out_of_range_sub_buffer) {
    BufferWriter bw(16);

    bw.write_u64(8);
    bw.write_u64(8);

    BufferReader br(bw.data().data(), bw.size());

    auto v1 = br.read_u64();
    ASSERT_TRUE(br.ok());
    ASSERT_EQ(v1, 8);
    ASSERT_EQ(br.remaining(), 8);

    auto sub = br.sub(10); /* out of range sub */
    ASSERT_FALSE(br.ok()); /* parent is poisioned */
    ASSERT_EQ(br.remaining(), 8);
    ASSERT_EQ(sub.remaining(), 0);
    ASSERT_EQ(sub.buffer_length(), 0);
}

TEST(BufferReader, poisoned_parent_poisoned_child) {
    BufferWriter bw(16);

    bw.write_u64(8);
    bw.write_u64(8);

    BufferReader br(bw.data().data(), bw.size());
    ASSERT_EQ(br.read_u64(), 8);
    ASSERT_EQ(br.read_u64(), 8);
    ASSERT_TRUE(br.ok());

    br.skip(1); /* poison */

    ASSERT_FALSE(br.ok());

    auto sub = br.sub(10);
    ASSERT_FALSE(sub.ok());
    ASSERT_EQ(sub.buffer_length(), 0);
}

TEST(BufferReader, subframe_should_not_read_beyond_provided_frame) {
    BufferWriter bw(48);

    bw.write_u64(10);
    bw.write_u64(20);
    bw.write_u64(30);
    bw.write_u64(40);
    bw.write_u64(50);
    bw.write_u64(60);

    BufferReader br(bw.data().data(), bw.size());
    ASSERT_EQ(br.read_u64(), 10);
    ASSERT_TRUE(br.ok());

    auto sub = br.sub(16);
    ASSERT_TRUE(sub.ok());

    /* parent moves past imm */
    ASSERT_EQ(br.remaining(), 24);

    /* sub should contain exactly 16 bytes */
    ASSERT_EQ(sub.remaining(), 16);
    ASSERT_EQ(sub.buffer_length(), 16);
    ASSERT_EQ(sub.read_u64(), 20);
    ASSERT_EQ(sub.read_u64(), 30);

    sub.skip(8); /* should poison sub NOT parent */

    ASSERT_FALSE(sub.ok());
    ASSERT_TRUE(br.ok()); /* parent should be fine */

    ASSERT_EQ(sub.read_u64(), 0); /* further reads return 0 for poisoned */

    ASSERT_EQ(br.remaining(), 24);

    ASSERT_EQ(br.read_u64(), 40);
    ASSERT_EQ(br.read_u64(), 50);
    ASSERT_EQ(br.read_u64(), 60);
    ASSERT_EQ(br.remaining(), 0);
}

TEST(BufferReader, read_bytes_returns_empty_on_failure) {
    BufferWriter bw(32);

    std::vector<uint8_t> bytes = {0x02, 0x01, 0xAA, 0xAB, 0xFF, 0xFF, 0x02, 0x07};

    /* write a bunc of data */
    bw.write_u8(121);
    bw.write_u16(1345);
    bw.write_bytes({bytes});
    bw.write_u32(235626);

    /* --- poisoned reader --- */
    BufferReader brp(bw.data().data(), bw.size());
    ASSERT_EQ(brp.read_u8(), 121);
    ASSERT_EQ(brp.read_u16(), 1345);
    auto gotp = brp.read_bytes(bytes.size() + 4 + /* invalid read */ 8);

    /* empty and ok is false */
    ASSERT_FALSE(brp.ok());
    ASSERT_TRUE(gotp.empty());

    /* --- good reader --- */
    BufferReader brg(bw.data().data(), bw.size());
    ASSERT_EQ(brg.read_u8(), 121);
    ASSERT_EQ(brg.read_u16(), 1345);
    auto gotg = brg.read_bytes(bytes.size());
    ASSERT_TRUE(std::equal(gotg.begin(), gotg.end(), bytes.begin(), bytes.end()));
    ASSERT_TRUE(brg.ok());
    ASSERT_EQ(brg.read_u32(), 235626);

    auto emp = brg.read_bytes(0);
    ASSERT_TRUE(emp.empty());
    ASSERT_TRUE(brg.ok());
}

TEST(BufferReader, skip_beyond_bounds) {
    BufferWriter bw(24);

    bw.write_u64(10);
    bw.write_u64(20);
    bw.write_u64(30);

    BufferReader br(bw.data().data(), bw.size());
    ASSERT_EQ(br.read_u64(), 10);

    br.skip(24); /* +8 more than the buf */

    ASSERT_FALSE(br.ok());
}

TEST(BufferReader, ctor_nullptr_check) {
    BufferReader br(nullptr, 10);
    ASSERT_FALSE(br.ok());
    ASSERT_TRUE(br.error().is_bad_config());
}

/* --------------------------------
 * BUFFER WRITER
 * -------------------------------- */

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

TEST(BufferWriter, patch_boundary) {
    BufferWriter w(32);

    /* --- 8 bit width --- */
    w.write_u8(10);
    ASSERT_TRUE(w.ok());

    /* offset == size() */
    w.patch_u8(8, 11);
    ASSERT_FALSE(w.ok());
    ASSERT_TRUE(w.error().is_out_of_range());
    w.clear();

    /* offset == size() - width */
    w.write_u8(10);
    ASSERT_TRUE(w.ok());
    w.patch_u8(0, 11);
    ASSERT_TRUE(w.ok());

    /* --- 16 bit width --- */
    w.write_u16(100);
    ASSERT_TRUE(w.ok());

    /* offset == size() */
    w.patch_u16(16, 101);
    ASSERT_FALSE(w.ok());
    ASSERT_TRUE(w.error().is_out_of_range());
    w.clear();

    /* offset == size() - width */
    w.write_u16(100);
    ASSERT_TRUE(w.ok());
    w.patch_u16(0, 101);
    ASSERT_TRUE(w.ok());

    /* --- 32 bit width --- */
    w.write_u32(1000);
    ASSERT_TRUE(w.ok());

    /* offset == size() */
    w.patch_u32(32, 1001);
    ASSERT_FALSE(w.ok());
    ASSERT_TRUE(w.error().is_out_of_range());
    w.clear();

    /* offset == size() - width */
    w.write_u32(1000);
    ASSERT_TRUE(w.ok());
    w.patch_u32(0, 1001);
    ASSERT_TRUE(w.ok());

    /* --- 64 bit width --- */
    w.write_u32(10000);
    ASSERT_TRUE(w.ok());

    /* offset == size() */
    w.patch_u64(64, 10001);
    ASSERT_FALSE(w.ok());
    ASSERT_TRUE(w.error().is_out_of_range());
    w.clear();

    /* offset == size() - width */
    w.write_u64(10000);
    ASSERT_TRUE(w.ok());
    w.patch_u64(0, 10001);
    ASSERT_TRUE(w.ok());
}

TEST(BufferWriter, patch_beyond_offset_NO_UB_UNDER_ASAN) {
    if (!ASAN_ENABLED) GTEST_SKIP() << "Skipping test: AddressSanitizer (ASan) is not enabled";

    BufferWriter w(32);
    w.write_u64(345678);

    ASSERT_TRUE(w.ok());

    w.patch_u64(32, 2345); /* patching beyond both capacity (i don't think matters) and size */

    /* NO UB */
    ASSERT_FALSE(w.ok());
}

TEST(BufferWriter, truncate) {
    BufferWriter w(32);
    w.write_u64(100);

    ASSERT_TRUE(w.ok());

    w.truncate(16, true); /* current is 8 bytes */
    ASSERT_FALSE(w.ok());

    w.clear();

    /* insert 3 post clear -> 24 */
    w.write_u64(100);
    w.write_u64(100);
    w.write_u64(100);

    ASSERT_EQ(w.size(), 24);

    w.truncate(24, true); /* noop */
    ASSERT_TRUE(w.ok());
    ASSERT_EQ(w.size(), 24);

    /* trunc 0 empties */
    w.truncate(0, true);
    ASSERT_TRUE(w.ok());
    ASSERT_EQ(w.size(), 0);
}

TEST(BufferWriter, poison_propagation) {
    BufferWriter w(32);
    w.write_u64(32);

    ASSERT_TRUE(w.ok());
    ASSERT_EQ(w.size(), 8);

    w.patch_u64(999, 999); /* poison writer */

    ASSERT_FALSE(w.ok());
    ASSERT_EQ(w.size(), 8);

    /* none of the methods should not impact */
    w.write_u8(1);
    ASSERT_FALSE(w.ok());
    ASSERT_EQ(w.size(), 8);

    w.patch_u8(0, 16);
    ASSERT_FALSE(w.ok());
    ASSERT_EQ(w.size(), 8);

    w.write_u16(1);
    ASSERT_FALSE(w.ok());
    ASSERT_EQ(w.size(), 8);

    w.patch_u16(0, 16);
    ASSERT_FALSE(w.ok());
    ASSERT_EQ(w.size(), 8);

    w.write_u32(1);
    ASSERT_FALSE(w.ok());
    ASSERT_EQ(w.size(), 8);

    w.patch_u32(0, 16);
    ASSERT_FALSE(w.ok());
    ASSERT_EQ(w.size(), 8);

    w.write_u64(1);
    ASSERT_FALSE(w.ok());
    ASSERT_EQ(w.size(), 8);

    w.patch_u64(0, 16);
    ASSERT_FALSE(w.ok());
    ASSERT_EQ(w.size(), 8);

    std::vector<uint8_t> value = {1, 2, 3, 4, 5};
    std::span<const uint8_t> vref{value};
    w.write_bytes(vref);
    ASSERT_FALSE(w.ok());
    ASSERT_EQ(w.size(), 8);

    auto s = w.reserve_slot(0);
    ASSERT_EQ(s, 0);
    ASSERT_FALSE(w.ok());
    ASSERT_EQ(w.size(), 8);
}

TEST(BufferWriter, clear) {
    BufferWriter w(32);
    w.write_u64(32);
}
