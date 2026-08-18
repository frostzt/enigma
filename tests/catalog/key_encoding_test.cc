#include "enigmadb/catalog/key_encoding.h"

#include <algorithm>
#include <vector>

#include "enigmadb/storage/key.h"
#include "gtest/gtest.h"
#include "test_support/keys.h"

using namespace enigmadb;
using namespace enigmadb::TESTNAMESPACE;

TEST(key_encoding, ordering_same_encoded_logical_order) {
    std::vector<storage::Key> keys{
        make_key("def", "delta", "e"), make_key("def", "delta", "f"), make_key("ghi", "gamma", "g"),
        make_key("ghi", "gamma", "i"), make_key("def", "delta", "d"), make_key("abc", "alpha", "b"),
        make_key("abc", "alpha", "c"), make_key("abc", "alpha", "a"), make_key("ghi", "gamma", "h"),
    };

    std::vector<storage::Key> expected{
        make_key("abc", "alpha", "a"), make_key("abc", "alpha", "b"), make_key("abc", "alpha", "c"),
        make_key("def", "delta", "d"), make_key("def", "delta", "e"), make_key("def", "delta", "f"),
        make_key("ghi", "gamma", "g"), make_key("ghi", "gamma", "h"), make_key("ghi", "gamma", "i"),
    };

    std::sort(keys.begin(), keys.end(), [](const storage::Key a, const storage::Key b) -> bool {
        return std::lexicographical_compare(a.bytes().begin(), a.bytes().end(), b.bytes().begin(), b.bytes().end());
    });

    ASSERT_EQ(keys, expected);
}

TEST(key_encoding, preserves_prefix_ordering) {
    auto key_short = make_key("ab", "c", "d");
    auto key_long = make_key("abc", "c", "d");

    ASSERT_TRUE(key_short < key_long);
}

TEST(key_encoding, preserves_embedded_null_ordering) {
    auto key_null = catalog::encode_composite_key(make_bytes({'a', 0x00, 'c'}), make_bytes({'d'}), make_bytes({'d'}));
    auto key_one = catalog::encode_composite_key(make_bytes({'a', 0x01, 'c'}), make_bytes({'d'}), make_bytes({'d'}));

    ASSERT_TRUE(key_null < key_one);
}

TEST(key_encoding, preserves_field_ordering) {
    auto first_key = catalog::encode_composite_key(make_bytes({'a', 'b'}), make_bytes({'b', 'b'}), make_bytes({'c'}));
    auto second_key = catalog::encode_composite_key(make_bytes({'a', 'a'}), make_bytes({'b', 'b'}), make_bytes({'c'}));

    ASSERT_TRUE(second_key < first_key);
}

TEST(key_decoding, rejects_truncated_buffers) {
    std::vector<uint8_t> incomp = {'a', 'b', 0x00};
    auto res = catalog::decode_composite_key(incomp);
    ASSERT_FALSE(res.has_value());
}

TEST(key_decoding, rejects_invalid_escape_sequences) {
    std::vector<uint8_t> invalid_escape = {'a', 0x00, 0x02, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01};
    auto res = catalog::decode_composite_key(invalid_escape);
    ASSERT_FALSE(res.has_value());
}

TEST(key_decoding, rejects_missing_fields) {
    std::vector<uint8_t> one_field = {'a', 'b', 0x00, 0x01};
    auto res = catalog::decode_composite_key(one_field);
    ASSERT_FALSE(res.has_value());
}
TEST(key_encoding, round_trip_binary_data) {
    std::vector<uint8_t> part = {0x00, 0xFF, 0x01, 0x00};
    std::vector<uint8_t> clust = {0xFF, 0x00, 0xFE};
    std::vector<uint8_t> col = {0x00, 0x00, 0x01, 0x01};

    auto encoded = catalog::encode_composite_key(part, clust, col);
    auto decoded_res = catalog::decode_composite_key(encoded);

    ASSERT_TRUE(decoded_res.has_value());
    auto decoded = decoded_res.value();

    EXPECT_EQ(decoded.partition_key, part);
    EXPECT_EQ(decoded.clustering_key, clust);
    EXPECT_EQ(decoded.column_name, col);
}

TEST(key_encoding, handles_empty_fields) {
    std::vector<uint8_t> empty;
    std::vector<uint8_t> val = {'a'};

    auto encoded = catalog::encode_composite_key(empty, val, empty);
    auto decoded_res = catalog::decode_composite_key(encoded);

    ASSERT_TRUE(decoded_res.has_value());
    auto decoded = decoded_res.value();

    EXPECT_EQ(decoded.partition_key, empty);
    EXPECT_EQ(decoded.clustering_key, val);
    EXPECT_EQ(decoded.column_name, empty);
}

TEST(key_encoding, order_by_cluster) {
    auto high = make_key("ab", "c", "d");
    auto low = make_key("ab", "b", "d");
    ASSERT_TRUE(low < high);
}

TEST(key_encoding, order_by_column_name) {
    auto high = make_key("ab", "c", "d");
    auto low = make_key("ab", "c", "c");
    ASSERT_TRUE(low < high);
}

TEST(key_encoding, order_by_partition) {
    auto high = make_key("abc", "aaa", "aaa");
    auto low = make_key("ab", "zzz", "zzz");
    ASSERT_TRUE(low < high);
}
