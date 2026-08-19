#include "enigmadb/utils/cache.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "test_support/keys.h"

using namespace enigmadb;
using namespace enigmadb::TESTNAMESPACE;

void* encode_value(uintptr_t v) { return reinterpret_cast<void*>(v); }
int decode_value(void* v) { return reinterpret_cast<uintptr_t>(v); }

std::vector<std::pair<const std::string_view, int>> deleted_values(20);

void clear_vec() { deleted_values.clear(); }

void deleter(const std::string_view k, void* v) { deleted_values.push_back(std::make_pair(k, decode_value(v))); }

TEST(Cache, gets_simple_value) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(100, 4));

    cache->release(cache->insert("asdf", encode_value(12), 8, &deleter));
    auto handle = cache->lookup("asdf");
    ASSERT_TRUE(handle != nullptr);

    auto value = decode_value(cache->value(handle));
    ASSERT_EQ(value, 12);
    cache->release(handle);
}

TEST(Cache, hits_and_misses) {
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

TEST(Cache, eviction_policy) {
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

TEST(Cache, replaces_key_and_deletes) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(100, 1));

    cache->release(cache->insert("new_key", encode_value(1), 2, &deleter));
    auto h = cache->lookup("new_key");
    auto v = decode_value(cache->value(h));
    auto stats = cache->get_stats();

    ASSERT_EQ(stats.total_evictions, 0);
    ASSERT_EQ(stats.total_hits, 1);
    ASSERT_EQ(stats.total_misses, 0);
    ASSERT_EQ(stats.total_inserts, 1);
    ASSERT_EQ(deleted_values.size(), 0);

    ASSERT_EQ(v, 1);
    cache->release(h);

    ASSERT_EQ(deleted_values.size(), 0);

    /* insert duplicate */
    cache->release(cache->insert("new_key", encode_value(1), 2, &deleter));
    h = cache->lookup("new_key");
    v = decode_value(cache->value(h));
    stats = cache->get_stats();

    ASSERT_EQ(stats.total_evictions, 0);
    ASSERT_EQ(stats.total_hits, 2);
    ASSERT_EQ(stats.total_misses, 0);
    ASSERT_EQ(stats.total_inserts, 2);
    ASSERT_EQ(deleted_values.size(), 1);

    cache->release(h);
}

TEST(Cache, erase) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(100, 1));

    cache->release(cache->insert("new_key", encode_value(1), 2, &deleter));
    auto h = cache->lookup("new_key");
    auto v = decode_value(cache->value(h));
    auto stats = cache->get_stats();

    ASSERT_EQ(stats.total_evictions, 0);
    ASSERT_EQ(stats.total_hits, 1);
    ASSERT_EQ(stats.total_misses, 0);
    ASSERT_EQ(stats.total_inserts, 1);
    ASSERT_EQ(v, 1);
    cache->release(h);

    cache->erase("new_key");
    h = cache->lookup("new_key");
    stats = cache->get_stats();
    ASSERT_EQ(h, nullptr);
    ASSERT_EQ(stats.total_evictions, 1);
    ASSERT_EQ(stats.total_hits, 1);
    ASSERT_EQ(stats.total_misses, 1);
    ASSERT_EQ(stats.total_inserts, 1);
}

TEST(Cache, erase_noop) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(100, 1));
    cache->erase("does_not_exist");
    auto stats = cache->get_stats();

    ASSERT_EQ(stats.total_evictions, 0);
    ASSERT_EQ(stats.total_hits, 0);
    ASSERT_EQ(stats.total_misses, 0);
    ASSERT_EQ(stats.total_inserts, 0);
}

TEST(Cache, new_id_increases_monotonically_single_thread) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(100, 1));

    for (size_t i = 1; i < 5000; i++) {
        auto gid = cache->new_id();
        ASSERT_EQ(i, gid);
    }
}

TEST(Cache, new_id_increases_monotonically_concurrent) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(100, 1));

    constexpr size_t numthreads = 8;
    constexpr size_t ids_per_thread = 1000;
    constexpr size_t total_ids = numthreads * ids_per_thread;

    std::vector<std::thread> threads;
    threads.reserve(numthreads);

    std::mutex mtx;
    std::vector<uint64_t> all_ids;
    all_ids.reserve(total_ids);

    for (size_t i = 0; i < numthreads; i++) {
        threads.emplace_back([&cache, &mtx, &all_ids]() {
            std::vector<uint64_t> local_ids;
            local_ids.reserve(ids_per_thread);

            uint64_t prev_id = 0;
            for (size_t i = 0; i < ids_per_thread; ++i) {
                auto gid = cache->new_id();

                EXPECT_GT(gid, prev_id);
                prev_id = gid;

                local_ids.push_back(gid);
            }

            std::lock_guard<std::mutex> lock(mtx);
            all_ids.insert(all_ids.end(), local_ids.begin(), local_ids.end());
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    /* ids must be unique */
    ASSERT_EQ(all_ids.size(), total_ids);

    std::sort(all_ids.begin(), all_ids.end());

    /* ids present exactly once */
    for (size_t i = 0; i < total_ids; ++i) {
        ASSERT_EQ(all_ids[i], static_cast<uint64_t>(i + 1));
    }
}

TEST(Cache, charge_increments_and_decrements) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(100, 1));

    ASSERT_EQ(cache->total_charge(), 0);

    cache->release(cache->insert("new_key", encode_value(1), 8, &deleter));
    auto h = cache->lookup("new_key");
    auto v = decode_value(cache->value(h));
    ASSERT_EQ(v, 1);

    ASSERT_EQ(cache->total_charge(), 8);
    cache->release(h);

    cache->erase("new_key");
    ASSERT_EQ(cache->total_charge(), 0);
}

TEST(Cache, lru_ordering) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(25, 1));
    auto stats = cache->get_stats();

    ASSERT_EQ(stats.total_evictions, 0);
    ASSERT_EQ(stats.total_hits, 0);
    ASSERT_EQ(stats.total_inserts, 0);
    ASSERT_EQ(stats.total_misses, 0);

    cache->release(cache->insert("red", encode_value(1), 8, &deleter));
    cache->release(cache->insert("blue", encode_value(2), 8, &deleter));
    cache->release(cache->insert("pink", encode_value(3), 8, &deleter));

    stats = cache->get_stats();

    ASSERT_EQ(stats.total_evictions, 0);
    ASSERT_EQ(stats.total_hits, 0);
    ASSERT_EQ(stats.total_inserts, 3);
    ASSERT_EQ(stats.total_misses, 0);

    cache->release(cache->insert("lancy", encode_value(3), 8, &deleter));

    auto h = cache->lookup("red");
    stats = cache->get_stats();
    ASSERT_EQ(h, nullptr);

    ASSERT_EQ(stats.total_evictions, 1);
    ASSERT_EQ(stats.total_hits, 0);
    ASSERT_EQ(stats.total_inserts, 4);
    ASSERT_EQ(stats.total_misses, 1);
}

TEST(Cache, lru_ordering_with_held_handle) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(25, 1));
    auto stats = cache->get_stats();

    ASSERT_EQ(stats.total_evictions, 0);
    ASSERT_EQ(stats.total_hits, 0);
    ASSERT_EQ(stats.total_inserts, 0);
    ASSERT_EQ(stats.total_misses, 0);

    cache->release(cache->insert("red", encode_value(1), 8, &deleter));
    cache->release(cache->insert("blue", encode_value(2), 8, &deleter));
    cache->release(cache->insert("pink", encode_value(3), 8, &deleter));

    /* should move red above */
    cache->release(cache->lookup("red"));

    stats = cache->get_stats();

    ASSERT_EQ(stats.total_evictions, 0);
    ASSERT_EQ(stats.total_hits, 1);
    ASSERT_EQ(stats.total_inserts, 3);
    ASSERT_EQ(stats.total_misses, 0);

    cache->release(cache->insert("lancy", encode_value(3), 8, &deleter));

    auto h = cache->lookup("red");
    auto v = decode_value(cache->value(h));
    stats = cache->get_stats();
    ASSERT_EQ(v, 1);

    cache->release(h);

    auto l = cache->lookup("blue");
    ASSERT_EQ(l, nullptr);

    ASSERT_EQ(stats.total_evictions, 1);
    ASSERT_EQ(stats.total_hits, 2);
    ASSERT_EQ(stats.total_inserts, 4);
    ASSERT_EQ(stats.total_misses, 0);
}

TEST(Cache, charge_exceeds_capacity_but_readable) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(25, 1));

    cache->release(cache->insert("red", encode_value(1), 30, &deleter));

    auto h = cache->lookup("red");
    auto v = decode_value(cache->value(h));
    ASSERT_EQ(v, 1);
    cache->release(h);
}

TEST(Cache, charge_exceeds_capacity_for_held_handle_and_resume_eviction) {
    clear_vec();
    constexpr size_t capacity = 24; /* 8 * 3 */
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(capacity, 1));

    std::vector<std::string> keys = {"a", "b", "c", "d", "e", "f"};

    auto h1 = cache->insert("a", encode_value(1), 8, &deleter);
    auto h2 = cache->insert("b", encode_value(2), 8, &deleter);
    auto h3 = cache->insert("c", encode_value(3), 8, &deleter);
    ASSERT_EQ(cache->total_charge(), capacity);
    /* now beyond capacity */
    auto h4 = cache->insert("d", encode_value(4), 8, &deleter);
    ASSERT_EQ(cache->total_charge(), capacity + 8);
    auto h5 = cache->insert("e", encode_value(5), 8, &deleter);
    ASSERT_EQ(cache->total_charge(), capacity + (8 * 2));
    auto h6 = cache->insert("f", encode_value(6), 8, &deleter);
    ASSERT_EQ(cache->total_charge(), capacity + (8 * 3));
    ASSERT_GT(cache->total_charge(), capacity);

    cache->release(h1);
    cache->release(h2);
    cache->release(h3);
    cache->release(h4);
    cache->release(h5);
    cache->release(h6);

    for (size_t i = 1; i <= keys.size(); i++) {
        auto l = cache->lookup(keys[i - 1]);
        ASSERT_NE(l, nullptr);
        auto v = decode_value(cache->value(l));
        ASSERT_EQ(v, i);
        cache->release(l);
    }

    /* We release the handles and insert new should evict the last 4 */
    cache->release(cache->insert("g", encode_value(7), 8, &deleter));

    /* last 3 should have been evicted */
    auto e1 = cache->lookup("a");
    ASSERT_EQ(e1, nullptr);
    auto e2 = cache->lookup("b");
    ASSERT_EQ(e2, nullptr);
    auto e3 = cache->lookup("c");
    ASSERT_EQ(e3, nullptr);
}

TEST(Cache, prune) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(50, 1));

    cache->release(cache->insert("key1", encode_value(1), 5, deleter));
    cache->release(cache->insert("key2", encode_value(2), 5, deleter));
    cache->release(cache->insert("key3", encode_value(3), 5, deleter));
    cache->release(cache->insert("key4", encode_value(4), 5, deleter));
    cache->release(cache->insert("key5", encode_value(5), 5, deleter));

    cache->prune();
    for (size_t i = 1; i <= 5; i++) {
        auto l = cache->lookup("key" + std::to_string(i));
        ASSERT_EQ(l, nullptr);
    }
}

TEST(Cache, prune_does_not_evict_pinned) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(50, 1));

    auto h1 = cache->insert("key1", encode_value(1), 5, deleter);
    auto h2 = cache->insert("key2", encode_value(2), 5, deleter);
    auto h3 = cache->insert("key3", encode_value(3), 5, deleter);
    auto h4 = cache->insert("key4", encode_value(4), 5, deleter);
    auto h5 = cache->insert("key5", encode_value(5), 5, deleter);

    cache->prune();

    for (size_t i = 1; i <= 5; i++) {
        auto l = cache->lookup("key" + std::to_string(i));
        ASSERT_NE(l, nullptr);
        auto v = decode_value(cache->value(l));
        ASSERT_EQ(v, i);
        cache->release(l);
    }

    cache->release(h1);
    cache->release(h2);
    cache->release(h3);
    cache->release(h4);
    cache->release(h5);
}

TEST(Cache, zero_capacity_returns_handle) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(0, 1));

    auto h = cache->insert("key", encode_value(1), 5, deleter);
    auto v = decode_value(cache->value(h));
    ASSERT_EQ(v, 1);

    cache->release(h);
    ASSERT_EQ(deleted_values.size(), 1);

    auto hi = cache->lookup("key");
    ASSERT_EQ(hi, nullptr);
}

TEST(Cache, general_lifecycle) {
    clear_vec();
    constexpr size_t N = 8;
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(N * 5, 1));

    /* write some keys */
    for (size_t i = 0; i < N; i++) {
        cache->release(cache->insert("key" + std::to_string(i), encode_value(i), 5, &deleter));
    }

    /* read values from keys */
    for (size_t i = 0; i < N; i++) {
        auto hi = cache->lookup("key" + std::to_string(i));
        ASSERT_NE(hi, nullptr);
        auto vi = decode_value(cache->value(hi));
        ASSERT_EQ(vi, i);
        cache->release(hi);
    }

    /* write some more keys should cause 5 evictions */
    for (size_t i = 8; i < 13; i++) {
        cache->release(cache->insert("key" + std::to_string(i), encode_value(1), 5, &deleter));
    }

    /* read evicted values */
    for (size_t i = 0; i < N - 3; i++) {
        auto hi = cache->lookup("key" + std::to_string(i));
        ASSERT_EQ(hi, nullptr);
    }

    for (size_t i = 5; i < N - 3; i++) {
        auto hi = cache->lookup("key" + std::to_string(i));
        ASSERT_NE(hi, nullptr);
        auto vi = decode_value(cache->value(hi));
        ASSERT_EQ(vi, i);
        cache->release(hi);
    }

    cache->prune();
}

TEST(Cache, destroy_cache_while_open_handle) {
    clear_vec();
    EXPECT_DEATH(
        {
            std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(25, 1));
            [[maybe_unused]] auto h1 = cache->insert("key1", encode_value(1), 5, &deleter);
            [[maybe_unused]] auto h2 = cache->insert("key2", encode_value(1), 5, &deleter);
        },
        "in_use_.next");
}

TEST(Cache, hashtable_growth) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(750, 1));

    /* hashtable grows *2 -> (start) 4 -> 8 -> 16 -> 32 -> 64 -> 128 */
    for (size_t i = 0; i < 129; i++) {
        cache->release(cache->insert("k" + std::to_string(i), encode_value(i), 5, &deleter));
    }

    for (size_t i = 0; i < 129; i++) {
        auto hi = cache->lookup("k" + std::to_string(i));
        ASSERT_NE(hi, nullptr);
        auto vi = decode_value(cache->value(hi));
        ASSERT_EQ(vi, i);
        cache->release(hi);
    }
}

TEST(Cache, embedded_terminator_bytes) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(750, 1));

    auto k1 = make_bytes({'a', 0x00, 'c'});
    auto k2 = make_bytes({0x00, 'a', 'c'});
    auto k3 = make_bytes({'a', 'c', 0x00});

    std::string_view sk1(reinterpret_cast<const char*>(k1.data()), k1.size());
    std::string_view sk2(reinterpret_cast<const char*>(k2.data()), k2.size());
    std::string_view sk3(reinterpret_cast<const char*>(k3.data()), k3.size());

    cache->release(cache->insert(sk1, encode_value(1), 5, &deleter));
    cache->release(cache->insert(sk2, encode_value(2), 5, &deleter));
    cache->release(cache->insert(sk3, encode_value(3), 5, &deleter));

    /* k1 should return value */
    auto h = cache->lookup(sk1);
    ASSERT_NE(h, nullptr);
    auto v = decode_value(cache->value(h));
    ASSERT_EQ(v, 1);
    cache->release(h);

    /* k2 should return value */
    h = cache->lookup(sk2);
    ASSERT_NE(h, nullptr);
    v = decode_value(cache->value(h));
    ASSERT_EQ(v, 2);
    cache->release(h);

    /* k3 should return value */
    h = cache->lookup(sk3);
    ASSERT_NE(h, nullptr);
    v = decode_value(cache->value(h));
    ASSERT_EQ(v, 3);
    cache->release(h);
}

TEST(Cache, empty_keys) {
    clear_vec();
    std::unique_ptr<utils::Cache> cache(utils::NewLRUCache(25, 1));

    cache->release(cache->insert("", encode_value(1), 5, &deleter));

    auto h = cache->lookup("");
    ASSERT_NE(h, nullptr);
    auto v = decode_value(cache->value(h));
    ASSERT_EQ(v, 1);
    cache->release(h);
}
