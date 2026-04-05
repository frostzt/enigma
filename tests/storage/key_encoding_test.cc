#include "enigmadb/storage/key_encoding.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "enigmadb/common/utils.h"
#include "gtest/gtest.h"

using namespace enigmadb::common;
using namespace enigmadb::storage;

TEST(key_encoding, different_partition_key_cmp) {
    auto alice_partition_key = encode_composite_key(
        string_to_bytes("alice"), string_to_bytes("x"), "col");
    auto bob_partition_key = encode_composite_key(string_to_bytes("bob"),
                                                  string_to_bytes("x"), "col");

    CompositeKeyComparator cmp;
    ASSERT_TRUE(cmp(alice_partition_key, bob_partition_key));
    ASSERT_FALSE(cmp(bob_partition_key, alice_partition_key));
}

TEST(key_encoding, different_clustering_key_cmp) {
    auto alice_partition_key = encode_composite_key(
        string_to_bytes("alice"), string_to_bytes("2026-02"), "col");
    auto alice_partition_key2 = encode_composite_key(
        string_to_bytes("alice"), string_to_bytes("2026-03"), "col");

    CompositeKeyComparator cmp;
    ASSERT_TRUE(cmp(alice_partition_key, alice_partition_key2));
    ASSERT_FALSE(cmp(alice_partition_key2, alice_partition_key));
}

TEST(key_encoding, different_column_cmp) {
    auto alice_partition_key = encode_composite_key(
        string_to_bytes("alice"), string_to_bytes("2026-02"), "age");
    auto alice_partition_key2 = encode_composite_key(
        string_to_bytes("alice"), string_to_bytes("2026-02"), "name");

    CompositeKeyComparator cmp;
    ASSERT_TRUE(cmp(alice_partition_key, alice_partition_key2));
    ASSERT_FALSE(cmp(alice_partition_key2, alice_partition_key));
}

TEST(key_encoding, identical_keys) {
    auto alice_partition_key = encode_composite_key(
        string_to_bytes("alice"), string_to_bytes("2026-02"), "name");
    auto alice_partition_key2 = encode_composite_key(
        string_to_bytes("alice"), string_to_bytes("2026-02"), "name");

    CompositeKeyComparator cmp;
    ASSERT_FALSE(cmp(alice_partition_key, alice_partition_key2));
}

TEST(key_encoding, map_comparator) {
    std::map<std::vector<uint8_t>, std::string, CompositeKeyComparator> store;

    auto alice_partition_key = encode_composite_key(
        string_to_bytes("alice"), string_to_bytes("2026-03"), "name");
    auto alice_partition_key2 = encode_composite_key(
        string_to_bytes("alice"), string_to_bytes("2026-02"), "age");
    auto bob_partition_key = encode_composite_key(
        string_to_bytes("bob"), string_to_bytes("2026-3"), "name");

    store.insert({alice_partition_key, "alice_name"});
    store.insert({alice_partition_key2, "alice_age"});
    store.insert({bob_partition_key, "bob_name"});

    std::vector<std::string> expected = {"alice_age", "alice_name", "bob_name"};
    std::vector<std::string> actual;

    for (const auto& [key, value] : store) {
        actual.push_back(value);
    }

    EXPECT_EQ(actual, expected);
}
