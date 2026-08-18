#include "enigmadb/storage/dazzle_db/sstable/table_cache.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <string>

#include "enigmadb/io/posix_io_engine.h"
#include "enigmadb/storage/dazzle_db/internal_value.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_writer.h"
#include "enigmadb/tempdir.h"
#include "enigmadb/utils.h"
#include "enigmadb/utils/cache.h"
#include "enigmadb/utils/numbers.h"
#include "test_support/keys.h"

/* TODO: One TODO here is that we probably wanna have this RAII compatible maybe create using a class or extend
 * `TempDir` in someway which allows doing something more extendable and useful */

using namespace enigmadb;
using namespace enigmadb::TESTNAMESPACE;

void setup_dir_and_files(std::string dir_path, size_t n, size_t num_kvs = 1000, size_t expected_keys_per_file = 5000) {
    io::PosixIOEngine engine;
    std::filesystem::create_directory(dir_path + "/sst"); /* sst dir */

    /* create n sst files - remember that the sst_path function creates ssts under data_dir/sst so this will follow the
     * same idea */
    for (size_t i = 0; i < n; i++) {
        auto cwr = dazzle::SSTableWriter::create(engine, dazzle::sst_path(dir_path, i), expected_keys_per_file);
        ASSERT_TRUE(cwr.has_value());

        auto& w = cwr.value();
        for (size_t k = 0; k < num_kvs; k++) {
            auto key = make_key("partition" + std::to_string(n) + std::to_string(i),
                                "cluster" + std::to_string(n) + std::to_string(i),
                                "column" + std::to_string(n) + std::to_string(i));
            auto val = dazzle::InternalValue{string_to_bytes("value" + std::to_string(n) + std::to_string(i)), false,
                                             (i * num_kvs) + k};

            ASSERT_TRUE(w.add(key, val).has_value());
        }

        ASSERT_TRUE(w.finish().has_value());
    }
}

TEST(TableCache, opens_file_once) {
    Tempdir dir_path("./table_cache_unit_tests");
    setup_dir_and_files("./table_cache_unit_tests", 5);

    io::PosixIOEngine engine;

    auto cache = utils::NewLRUCache(mb_to_b(32), 16);
    auto tcr = dazzle::TableCache::create(engine, "./table_cache_unit_tests", std::move(cache));
    ASSERT_TRUE(tcr.has_value());

    auto& tc = tcr.value();

    auto first_stats = tc->get_stats();
    ASSERT_EQ(first_stats.total_hits, 0);
    ASSERT_EQ(first_stats.total_misses, 0);

    auto sstfound = tc->get(dazzle::SSTableId{1});
    ASSERT_TRUE(sstfound.has_value());

    auto second_stats = tc->get_stats();
    ASSERT_EQ(second_stats.total_hits, 0);
    ASSERT_EQ(second_stats.total_misses, 1);

    sstfound = tc->get(dazzle::SSTableId{1});
    ASSERT_TRUE(sstfound.has_value());

    auto third_stats = tc->get_stats();
    ASSERT_EQ(third_stats.total_hits, 1);
    ASSERT_EQ(third_stats.total_misses, 1);
}

TEST(TableCache, auto_eviction) {
    Tempdir dir_path("./table_cache_unit_tests");
    setup_dir_and_files("./table_cache_unit_tests", 5);

    io::PosixIOEngine engine;

    auto cache = utils::NewLRUCache(22000, 1);  // 7242 each sstreader in cache
    auto tcr = dazzle::TableCache::create(engine, "./table_cache_unit_tests", std::move(cache));
    ASSERT_TRUE(tcr.has_value());

    auto& tc = tcr.value();

    auto first_stats = tc->get_stats();
    ASSERT_EQ(first_stats.total_hits, 0);
    ASSERT_EQ(first_stats.total_misses, 0);

    {
        auto sstfound1 = tc->get(dazzle::SSTableId{1});
        ASSERT_TRUE(sstfound1.has_value());
        auto sstfound2 = tc->get(dazzle::SSTableId{2});
        ASSERT_TRUE(sstfound2.has_value());
        auto sstfound3 = tc->get(dazzle::SSTableId{3});
        ASSERT_TRUE(sstfound3.has_value());

        auto second_stats = tc->get_stats();
        ASSERT_EQ(second_stats.total_hits, 0);
        ASSERT_EQ(second_stats.total_misses, 3);
        ASSERT_EQ(second_stats.total_evictions, 0);
    }

    auto sstfound4 = tc->get(dazzle::SSTableId{4});
    ASSERT_TRUE(sstfound4.has_value());

    auto third_stats = tc->get_stats();
    ASSERT_EQ(third_stats.total_hits, 0);
    ASSERT_EQ(third_stats.total_misses, 4);
    ASSERT_EQ(third_stats.total_evictions, 1);
}

TEST(TableCache, manual_eviction) {
    Tempdir dir_path("./table_cache_unit_tests");
    setup_dir_and_files("./table_cache_unit_tests", 5);

    io::PosixIOEngine engine;

    auto cache = utils::NewLRUCache(22000, 1);  // 7242 each sstreader in cache
    auto tcr = dazzle::TableCache::create(engine, "./table_cache_unit_tests", std::move(cache));
    ASSERT_TRUE(tcr.has_value());

    auto& tc = tcr.value();

    auto stats = tc->get_stats();
    ASSERT_EQ(stats.total_hits, 0);
    ASSERT_EQ(stats.total_misses, 0);

    {
        auto sstfound1 = tc->get(dazzle::SSTableId{1});
        ASSERT_TRUE(sstfound1.has_value());
        auto sstfound2 = tc->get(dazzle::SSTableId{2});
        ASSERT_TRUE(sstfound2.has_value());
        auto sstfound3 = tc->get(dazzle::SSTableId{3});
        ASSERT_TRUE(sstfound3.has_value());

        auto second_stats = tc->get_stats();
        ASSERT_EQ(second_stats.total_hits, 0);
        ASSERT_EQ(second_stats.total_misses, 3);
        ASSERT_EQ(second_stats.total_evictions, 0);
    }

    tc->evict(dazzle::SSTableId{3});

    stats = tc->get_stats();
    ASSERT_EQ(stats.total_hits, 0);
    ASSERT_EQ(stats.total_misses, 3);
    ASSERT_EQ(stats.total_evictions, 1);
}
