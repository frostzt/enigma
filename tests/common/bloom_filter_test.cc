#include "enigmadb/common/bloom_filter.h"

#include <gtest/gtest.h>

#include "enigmadb/common/utils.h"

using namespace enigmadb::common;

TEST(bloom_filter, add_may_contain_true) {
    BloomFilter filter{250, 0.01};

    filter.add(string_to_bytes("alice"));

    ASSERT_TRUE(filter.may_contain(string_to_bytes("alice")));
    ASSERT_FALSE(filter.may_contain(string_to_bytes("not_alice")));
}
