#include "enigmadb/storage/dazzle_db/core/version_edit.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "enigmadb/buffer.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"

using namespace enigmadb;

namespace {

dazzle::VersionEdit make_edit(size_t r, size_t a, std::optional<uint64_t> next_sst_id) {
    std::vector<dazzle::SSTableId> removed;
    removed.reserve(r);
    for (size_t i = 0; i < r; i++) {
        removed.push_back(dazzle::SSTableId{100 + i});
    }

    std::vector<dazzle::SSTableMeta> added;
    added.reserve(a);
    for (size_t i = 0; i < a; i++) {
        added.push_back(dazzle::SSTableMeta{
            .id = dazzle::SSTableId{200 + i}, .size_bytes = 1000 + i, .entry_count = 500 + i, .max_sequence = 50 + i});
    }

    return dazzle::VersionEdit{std::move(removed), std::move(added), next_sst_id};
}

/// Writes one framed record and returns the bytes, so a test can damage them.
std::vector<uint8_t> framed_bytes(const dazzle::VersionEdit& ve) {
    BufferWriter bw(256);
    auto w = write_framed(bw, [&](BufferWriter& b) { dazzle::encode_version_edit(b, ve); });
    EXPECT_TRUE(w.has_value());
    return std::vector<uint8_t>(bw.data().begin(), bw.data().end());
}

/// Reads one framed VersionEdit back out of a byte buffer.
Result<dazzle::VersionEdit> read_edit(const std::vector<uint8_t>& bytes) {
    BufferReader br(bytes.data(), bytes.size());
    return read_framed<dazzle::VersionEdit>(br, [](BufferReader& r) { return dazzle::decode_version_edit(r); });
}

void expect_same(const dazzle::VersionEdit& lhs, const dazzle::VersionEdit& rhs) {
    ASSERT_EQ(lhs.next_sst_id, rhs.next_sst_id);

    ASSERT_EQ(lhs.removed.size(), rhs.removed.size());
    for (size_t i = 0; i < lhs.removed.size(); i++) {
        EXPECT_EQ(lhs.removed[i].value, rhs.removed[i].value) << "removed[" << i << "]";
    }

    ASSERT_EQ(lhs.added.size(), rhs.added.size());
    for (size_t i = 0; i < lhs.added.size(); i++) {
        EXPECT_EQ(lhs.added[i].id.value, rhs.added[i].id.value) << "added[" << i << "].id";
        EXPECT_EQ(lhs.added[i].size_bytes, rhs.added[i].size_bytes) << "added[" << i << "].size_bytes";
        EXPECT_EQ(lhs.added[i].entry_count, rhs.added[i].entry_count) << "added[" << i << "].entry_count";
        EXPECT_EQ(lhs.added[i].max_sequence, rhs.added[i].max_sequence) << "added[" << i << "].max_sequence";
    }
}

}  // namespace

/* --------------------------------------------------------------------------
 * Payload codec — no framing involved
 * -------------------------------------------------------------------------- */

TEST(VersionEdit, round_trip_payload) {
    auto original = make_edit(5, 5, 1);

    BufferWriter bw(256);
    dazzle::encode_version_edit(bw, original);
    ASSERT_TRUE(bw.ok());

    BufferReader br(bw.data().data(), bw.size());
    auto decoded = dazzle::decode_version_edit(br);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(br.ok());
    EXPECT_EQ(br.remaining(), 0u) << "decoder left bytes unconsumed";

    expect_same(original, decoded.value());
}

TEST(VersionEdit, round_trip_payload_without_watermark) {
    auto original = make_edit(2, 3, std::nullopt);

    BufferWriter bw(256);
    dazzle::encode_version_edit(bw, original);
    ASSERT_TRUE(bw.ok());

    BufferReader br(bw.data().data(), bw.size());
    auto decoded = dazzle::decode_version_edit(br);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_FALSE(decoded.value().next_sst_id.has_value());
    EXPECT_EQ(br.remaining(), 0u);

    expect_same(original, decoded.value());
}

TEST(VersionEdit, round_trip_payload_empty) {
    auto original = make_edit(0, 0, std::nullopt);

    BufferWriter bw(64);
    dazzle::encode_version_edit(bw, original);
    ASSERT_TRUE(bw.ok());

    /* 4 (removed count) + 4 (added count) + 1 (flag) */
    EXPECT_EQ(bw.size(), 9u);

    BufferReader br(bw.data().data(), bw.size());
    auto decoded = dazzle::decode_version_edit(br);

    ASSERT_TRUE(decoded.has_value());
    expect_same(original, decoded.value());
}

/* --------------------------------------------------------------------------
 * Framed record — length + CRC live here, not in the payload codec
 * -------------------------------------------------------------------------- */

TEST(VersionEdit, round_trip_framed) {
    auto original = make_edit(5, 5, 1);

    auto bytes = framed_bytes(original);
    auto decoded = read_edit(bytes);

    ASSERT_TRUE(decoded.has_value());
    expect_same(original, decoded.value());
}

TEST(VersionEdit, detects_corruption) {
    auto original = make_edit(5, 5, 1);
    auto bytes = framed_bytes(original);

    /* Byte 12 is inside the body — 0..7 is the length + CRC header. */
    ASSERT_GT(bytes.size(), 12u);
    bytes[12] ^= 0xFF;

    auto decoded = read_edit(bytes);

    ASSERT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error().code, ErrorCode::CHECKSUM_MISMATCH);
}

TEST(VersionEdit, detects_corruption_at_every_body_byte) {
    auto original = make_edit(3, 3, 7);
    const auto clean = framed_bytes(original);

    /* Every single-byte flip in the body must be caught by the CRC. */
    for (size_t i = 8; i < clean.size(); i++) {
        auto bytes = clean;
        bytes[i] ^= 0xFF;

        auto decoded = read_edit(bytes);
        EXPECT_FALSE(decoded.has_value()) << "flip at byte " << i << " went undetected";
    }
}

TEST(VersionEdit, truncated_buffer) {
    auto original = make_edit(5, 5, 1);
    const auto clean = framed_bytes(original);

    for (size_t len = 0; len < clean.size(); len++) {
        std::vector<uint8_t> truncated(clean.begin(), clean.begin() + len);

        auto decoded = read_edit(truncated);

        ASSERT_FALSE(decoded.has_value()) << "prefix of length " << len << " decoded as valid";
        EXPECT_EQ(decoded.error().code, ErrorCode::INCOMPLETE_RECORD) << "prefix of length " << len;
    }

    EXPECT_TRUE(read_edit(clean).has_value());
}
