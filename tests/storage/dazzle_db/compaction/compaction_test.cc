#include <filesystem>
#include <memory>
#include <string>

#include "enigmadb/io/posix_io_engine.h"
#include "enigmadb/storage/dazzle_db/compaction/compaction_policy.h"
#include "enigmadb/storage/dazzle_db/dazzle_engine.h"
#include "enigmadb/tempdir.h"
#include "enigmadb/utils.h"
#include "gtest/gtest.h"
#include "test_support/keys.h"

namespace fs = std::filesystem;

using namespace enigmadb;
using namespace enigmadb::TESTNAMESPACE;

TEST(compaction, size_tiered_compaction_auto) {
    io::PosixIOEngine engine;
    std::string data_dir_path = "./compaction_tests";
    Tempdir testdir(data_dir_path);

    auto storage_engine_result = dazzle::Dazzle::open(engine, "./compaction_tests", 100000,
                                                      std::make_unique<dazzle::SizeTieredCompactionPolicy>(8, 16));
    ASSERT_TRUE(storage_engine_result.has_value());

    auto& storage_engine = storage_engine_result.value();

    /* flush calls for sure will create new sst files */
    for (size_t i = 0; i < 8; i++) {
        /* each sstable will have 1000 entries */
        for (size_t k = 0; k < 1000; k++) {
            auto key = make_key("alice__" + std::to_string(i) + "__" + std::to_string(k), "2026-01", "age");
            ASSERT_TRUE(storage_engine->put(key, string_to_bytes("1234" + std::to_string(i) + "__" + std::to_string(k)))
                            .has_value());
        }

        storage_engine->flush();
    }

    /* Delete even keys */
    for (size_t i = 0; i < 8; i++) {
        for (size_t k = 0; k < 1000; k++) {
            if (k % 2 == 0) continue;
            auto key = make_key("alice__" + std::to_string(i) + "__" + std::to_string(k), "2026-01", "age");
            ASSERT_TRUE(storage_engine->remove(key).has_value());
        }

        storage_engine->flush();
    }

    // auto count = std::count_if(fs::directory_iterator(testdir.path + "/sst"), fs::directory_iterator{},
    //                            [](const auto& entry) { return fs::is_regular_file(entry); });

    /* Based on the above config we set at this point we've hit the 16th file
     * this should auto compact based on the new implementation in flush() */
    // ASSERT_EQ(count, 2);

    /* should get all entries back post deletes */
    for (size_t i = 0; i < 8; i++) {
        for (size_t k = 0; k < 1000; k++) {
            auto key = make_key("alice__" + std::to_string(i) + "__" + std::to_string(k), "2026-01", "age");
            if (k % 2 != 0) {
                auto vr = storage_engine->get(key);
                ASSERT_FALSE(vr.value().has_value());
            } else {
                auto vr = storage_engine->get(key);
                ASSERT_TRUE(vr.has_value());
                ASSERT_EQ(bytes_to_string(vr.value().value().data),
                          "1234" + std::to_string(i) + "__" + std::to_string(k));
            }
        }
    }
}

TEST(compaction, full_compaction_manual) {
    io::PosixIOEngine engine;
    std::string data_dir_path = "./compaction_tests";
    Tempdir testdir(data_dir_path);

    auto storage_engine_result = dazzle::Dazzle::open(engine, "./compaction_tests", 100000); /* huge pre-emptively :) */
    ASSERT_TRUE(storage_engine_result.has_value());

    auto& storage_engine = storage_engine_result.value();
    /* Set to extreme values to prevent auto compaction */
    ASSERT_TRUE(storage_engine->set_compaction_policy(std::make_unique<dazzle::SizeTieredCompactionPolicy>(80, 160))
                    .has_value());

    /* flush calls for sure will create new sst files */
    for (size_t i = 0; i < 8; i++) {
        /* each sstable will have 1000 entries */
        for (size_t k = 0; k < 1000; k++) {
            auto key = make_key("alice__" + std::to_string(i) + "__" + std::to_string(k), "2026-01", "age");
            ASSERT_TRUE(storage_engine->put(key, string_to_bytes("1234" + std::to_string(i) + "__" + std::to_string(k)))
                            .has_value());
        }

        storage_engine->flush();
    }

    size_t total_deleted = 0;

    /* Delete even keys */
    for (size_t i = 0; i < 8; i++) {
        for (size_t k = 0; k < 1000; k++) {
            if (k % 2 == 0) continue;
            auto key = make_key("alice__" + std::to_string(i) + "__" + std::to_string(k), "2026-01", "age");
            ASSERT_TRUE(storage_engine->remove(key).has_value());
            total_deleted++;
        }

        storage_engine->flush();
    }

    auto count_initial = std::count_if(fs::directory_iterator(testdir.path + "/sst"), fs::directory_iterator{},
                                       [](const auto& entry) { return fs::is_regular_file(entry); });

    ASSERT_EQ(count_initial, 16);

    auto c_res = storage_engine->compact_now();
    ASSERT_TRUE(c_res.has_value());
    ASSERT_TRUE(c_res.value().has_value());

    auto count_post = std::count_if(fs::directory_iterator(testdir.path + "/sst"), fs::directory_iterator{},
                                    [](const auto& entry) { return fs::is_regular_file(entry); });

    ASSERT_EQ(count_post, 1);

    /* should get all entries back post deletes */
    for (size_t i = 0; i < 8; i++) {
        for (size_t k = 0; k < 1000; k++) {
            auto key = make_key("alice__" + std::to_string(i) + "__" + std::to_string(k), "2026-01", "age");
            if (k % 2 != 0) {
                auto vr = storage_engine->get(key);
                ASSERT_FALSE(vr.value().has_value());
            } else {
                auto vr = storage_engine->get(key);
                ASSERT_TRUE(vr.has_value());
                ASSERT_EQ(bytes_to_string(vr.value().value().data),
                          "1234" + std::to_string(i) + "__" + std::to_string(k));
            }
        }
    }

    auto sstr = dazzle::SSTableReader::create(engine, dazzle::sst_path(testdir.path, c_res.value().value().value));
    ASSERT_TRUE(sstr.has_value());
    auto& sst_reader = sstr.value();
    auto sst_itr = sst_reader.iterator();

    size_t found = 0;
    for (sst_itr.seek_to_first(); sst_itr.valid(); sst_itr.next()) {
        const auto value = sst_itr.value();
        ASSERT_FALSE(value.is_tombstone);
        found++;
    }

    ASSERT_EQ(
        /* total entries */ 8000 - /* total entries we looped over */ found, total_deleted);
}
