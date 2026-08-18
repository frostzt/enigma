#include "enigmadb/bloom_filter.h"

#include <gtest/gtest.h>

#include <string>

#include "enigmadb/utils.h"

using namespace enigmadb;

TEST(bloom_filter, add_may_contain_true) {
    BloomFilter filter{250, 0.01};

    filter.add(storage::Key{string_to_bytes("alice")});

    ASSERT_TRUE(filter.may_contain(storage::Key{string_to_bytes("alice")}));
    ASSERT_FALSE(filter.may_contain(storage::Key{string_to_bytes("not_alice")}));
}

TEST(bloom_filter, multiple_keys) {
    BloomFilter filter{500, 0.01};

    for (size_t i = 0; i < 500; i++) {
        if (i % 2 == 0) {
            filter.add(storage::Key{string_to_bytes("alice_" + std::to_string(i))});
        }
    }

    for (size_t i = 0; i < 500; i++) {
        if (i % 2 == 0) {
            ASSERT_TRUE(filter.may_contain(storage::Key{string_to_bytes("alice_" + std::to_string(i))}));
        }
    }
}
