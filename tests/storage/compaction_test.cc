#include "enigmadb/storage/compaction/compaction.h"

#include <filesystem>
#include <string>

#include "enigmadb/common/tempdir.h"
#include "enigmadb/common/utils.h"
#include "enigmadb/io/posix_io_engine.h"
#include "enigmadb/storage/common.h"
#include "enigmadb/storage/storage_engine.h"
#include "gtest/gtest.h"

namespace fs = std::filesystem;

using namespace enigmadb::io;
using namespace enigmadb::common;
using namespace enigmadb::storage;
using namespace enigmadb::storage::sstable;
using namespace enigmadb::storage::compaction;

TEST(compaction, size_tiered_full_compaction) {
    PosixIOEngine engine;
    std::string data_dir_path = "./compaction_tests";
    Tempdir testdir(data_dir_path);

    auto compactor = Compactor::create(engine, "./compaction_tests");

    auto storage_engine_result = StorageEngine::open(
        engine, "./compaction_tests", 100000); /* huge pre-emptively :) */
    ASSERT_TRUE(storage_engine_result.has_value());

    auto& storage_engine = storage_engine_result.value();
    ASSERT_TRUE(
        storage_engine
            ->set_compaction_config(compaction::SizeTieredConfig{16, 20})
            .has_value());

    /* flush calls for sure will create new sst files */
    for (size_t i = 0; i < 8; i++) {
        /* each sstable will have 1000 entries */
        for (size_t k = 0; k < 1000; k++) {
            ASSERT_TRUE(
                storage_engine
                    ->put(string_to_bytes("alice__" + std::to_string(i) + "__" +
                                          std::to_string(k)),
                          string_to_bytes("2026-01"), "age",
                          string_to_bytes("1234" + std::to_string(i) + "__" +
                                          std::to_string(k)))
                    .has_value());
        }

        storage_engine->flush();
    }

    size_t total_deleted = 0;

    /* Delete even keys */
    for (size_t i = 0; i < 8; i++) {
        for (size_t k = 0; k < 1000; k++) {
            if (k % 2 == 0) continue;
            ASSERT_TRUE(
                storage_engine
                    ->remove(string_to_bytes("alice__" + std::to_string(i) +
                                             "__" + std::to_string(k)),
                             string_to_bytes("2026-01"), "age")
                    .has_value());
            total_deleted++;
        }

        storage_engine->flush();
    }

    auto count = std::count_if(
        fs::directory_iterator(testdir.path + "/sst"), fs::directory_iterator{},
        [](const auto& entry) { return fs::is_regular_file(entry); });

    ASSERT_EQ(count, 16);

    auto next_id = storage_engine->do_compact_work();
    ASSERT_TRUE(next_id.has_value());

    /* should get all entries back post deletes */
    for (size_t i = 0; i < 8; i++) {
        for (size_t k = 0; k < 1000; k++) {
            if (k % 2 != 0) {
                auto vr = storage_engine->get(
                    string_to_bytes("alice__" + std::to_string(i) + "__" +
                                    std::to_string(k)),
                    string_to_bytes("2026-01"), "age");
                ASSERT_FALSE(vr.value().has_value());
            } else {
                auto vr = storage_engine->get(
                    string_to_bytes("alice__" + std::to_string(i) + "__" +
                                    std::to_string(k)),
                    string_to_bytes("2026-01"), "age");
                ASSERT_TRUE(vr.has_value());
                ASSERT_EQ(
                    bytes_to_string(vr.value().value().data),
                    "1234" + std::to_string(i) + "__" + std::to_string(k));
            }
        }
    }

    auto sstr = SSTableReader::create(
        engine, sst_path(testdir.path, next_id.value().value));
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
        /* total entries */ 8000 - /* total entries we looped over */ found,
        total_deleted);
}
