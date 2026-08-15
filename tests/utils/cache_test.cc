#include "enigmadb/utils/cache.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

using namespace enigmadb;

void* encode_value(uintptr_t v) { return reinterpret_cast<void*>(v); }
int decode_value(void* v) { return reinterpret_cast<uintptr_t>(v); }

std::vector<std::pair<const std::string_view, int>> deleted_values(20);

void clear_vec() { deleted_values.clear(); }

void deleter(const std::string_view k, void* v) { deleted_values.push_back(std::make_pair(k, decode_value(v))); }

TEST(cache, gets_simple_value) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(100, 4));

    cache->release(cache->insert("asdf", encode_value(12), 8, &deleter));
    auto handle = cache->lookup("asdf");
    ASSERT_TRUE(handle != nullptr);

    auto value = decode_value(cache->value(handle));
    ASSERT_EQ(value, 12);
    cache->release(handle);
}

TEST(cache, hits_and_misses) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(100, 1));

    auto h = cache->lookup("does_not_exist");
    ASSERT_EQ(h, nullptr);

    cache->release(cache->insert("a", encode_value(1), 2, &deleter));
    h = cache->lookup("a");
    auto v = decode_value(cache->value(h));
    ASSERT_EQ(v, 1);
    cache->release(h);
}

TEST(cache, eviction_policy) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(500, 1));

    cache->release(cache->insert("blue", encode_value(26), 1, &deleter));
    cache->release(cache->insert("imposter", encode_value(265), 1, &deleter));
    cache->release(cache->insert("red", encode_value(252), 1, &deleter));

    auto h = cache->lookup("red");

    for (size_t i = 0; i < 500 + 100; i++) {
        cache->release(cache->insert("sus" + std::to_string(i), encode_value(252 + i), 1, &deleter));

        auto lr = cache->lookup("sus" + std::to_string(i));
        auto lv = decode_value(cache->value(lr));
        ASSERT_EQ(lv, 252 + i);
        cache->release(lr);

        auto cr = cache->lookup("blue");
        auto cv = decode_value(cache->value(cr));
        ASSERT_EQ(cv, 26);
        cache->release(cr);
    }

    /* blue MUST exist */
    auto bh = cache->lookup("blue");
    auto bv = decode_value(cache->value(bh));
    ASSERT_EQ(bv, 26);
    cache->release(bh);

    /* imposter is evicted */
    auto ih = cache->lookup("imposter");
    ASSERT_EQ(ih, nullptr);

    /* red MUST exist */
    auto rh = cache->lookup("red");
    auto rv = decode_value(cache->value(rh));
    ASSERT_EQ(rv, 252);
    cache->release(rh);

    cache->release(h);
}
