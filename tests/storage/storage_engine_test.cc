#include "enigmadb/storage/storage_engine.h"

#include <filesystem>
#include <string>

#include "enigmadb/common/tempdir.h"
#include "enigmadb/common/utils.h"
#include "enigmadb/io/posix_io_engine.h"
#include "gtest/gtest.h"

using namespace enigmadb::io;
using namespace enigmadb::storage;
using namespace enigmadb::common;

TEST(StorageEngine, basic_flow) {
    PosixIOEngine engine;
    std::string data_dir_path = "./storage_engine_tests";
    Tempdir testdir(data_dir_path);

    auto storage_engine_result =
        StorageEngine::open(engine, data_dir_path, 1024);
    ASSERT_TRUE(storage_engine_result.has_value());

    auto& storage_engine = storage_engine_result.value();
    ASSERT_TRUE(storage_engine
                    .put(string_to_bytes("alice"), string_to_bytes("2026-01"),
                         "age", string_to_bytes("1234"))
                    .has_value());

    auto get_result = storage_engine.get(string_to_bytes("alice"),
                                         string_to_bytes("2026-01"), "age");
    ASSERT_TRUE(get_result.has_value());
    ASSERT_TRUE(get_result.value().has_value());

    auto value = get_result.value().value();
    auto value_str = bytes_to_string(value.data);
    ASSERT_EQ(value_str, "1234");
}

TEST(StorageEngine, flush_and_read_sstable) {
    PosixIOEngine engine;
    std::string data_dir_path = "./storage_engine_tests";
    Tempdir testdir(data_dir_path);

    auto storage_engine_result =
        StorageEngine::open(engine, data_dir_path, 512);
    ASSERT_TRUE(storage_engine_result.has_value());

    auto& storage_engine = storage_engine_result.value();

    for (size_t i = 10; i < 160; ++i) {
        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("alice"),
                             string_to_bytes("2026-" + std::to_string(i)),
                             "age" + std::to_string(i),
                             string_to_bytes("12" + std::to_string(i)))
                        .has_value());
    }

    for (size_t i = 10; i < 160; ++i) {
        auto result =
            storage_engine.get(string_to_bytes("alice"),
                               string_to_bytes("2026-" + std::to_string(i)),
                               "age" + std::to_string(i));

        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(result.value().has_value());

        auto value = result.value().value();
        ASSERT_EQ(bytes_to_string(value.data), "12" + std::to_string(i));
    }
}

TEST(StorageEngine, crash_recovery) {
    Tempdir testdir("./storage_engine_tests");

    {
        /* store and crash */
        std::string data_dir_path = "./storage_engine_tests";
        PosixIOEngine engine;
        auto storage_engine_result =
            StorageEngine::open(engine, data_dir_path, 500000);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("alice"),
                             string_to_bytes("2026-05"), "age",
                             string_to_bytes("12"))
                        .has_value());
    }

    {
        std::string data_dir_path = "./storage_engine_tests";
        PosixIOEngine engine;
        auto storage_engine_result =
            StorageEngine::open(engine, data_dir_path, 500000);

        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        auto res = storage_engine.get(string_to_bytes("alice"),
                                      string_to_bytes("2026-05"), "age");

        ASSERT_TRUE(res.has_value());
        ASSERT_TRUE(res.value().has_value());

        auto value = res.value().value();
        ASSERT_EQ(bytes_to_string(value.data), "12");
    }
}

TEST(StorageEngine, delete_shadowing_across_layers) {
    PosixIOEngine engine;
    std::string data_dir_path = "./storage_engine_tests";
    Tempdir testdir(data_dir_path);

    auto storage_engine_result =
        StorageEngine::open(engine, data_dir_path, 512);
    ASSERT_TRUE(storage_engine_result.has_value());

    auto& storage_engine = storage_engine_result.value();

    ASSERT_TRUE(storage_engine
                    .put(string_to_bytes("alice"), string_to_bytes("2026-05"),
                         "age", string_to_bytes("12"))
                    .has_value());

    ASSERT_TRUE(storage_engine.flush().has_value());

    auto res = storage_engine.get(string_to_bytes("alice"),
                                  string_to_bytes("2026-05"), "age");

    ASSERT_TRUE(res.has_value());
    ASSERT_TRUE(res.value().has_value());

    auto value = res.value().value();
    ASSERT_EQ(bytes_to_string(value.data), "12");

    ASSERT_TRUE(
        storage_engine
            .remove(string_to_bytes("alice"), string_to_bytes("2026-05"), "age")
            .has_value());

    ASSERT_TRUE(storage_engine.flush().has_value());

    auto res2 = storage_engine.get(string_to_bytes("alice"),
                                   string_to_bytes("2026-05"), "age");

    ASSERT_TRUE(res2.has_value());
    ASSERT_FALSE(res2.value().has_value());
}

TEST(StorageEngine, handle_empty_memtable_flush) {
    PosixIOEngine engine;
    std::string data_dir_path = "./storage_engine_tests";
    Tempdir testdir(data_dir_path);

    auto storage_engine_result =
        StorageEngine::open(engine, data_dir_path, 512);
    ASSERT_TRUE(storage_engine_result.has_value());

    auto& storage_engine = storage_engine_result.value();

    ASSERT_TRUE(storage_engine.flush().has_value());

    std::filesystem::path p = "./storage_engine_tests/sst";

    auto count = std::distance(std::filesystem::directory_iterator(p),
                               std::filesystem::directory_iterator{});
    ASSERT_TRUE(count == 0);
}

TEST(StorageEngine, high_water_mark_from_sstable) {
    std::string data_dir_path = "./storage_engine_tests";
    Tempdir testdir(data_dir_path);

    {
        PosixIOEngine engine;
        auto storage_engine_result =
            StorageEngine::open(engine, "./storage_engine_tests", 512);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("alice"),
                             string_to_bytes("2026-05"), "age",
                             string_to_bytes("32"))
                        .has_value());
        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("john"),
                             string_to_bytes("2026-05"), "age",
                             string_to_bytes("30"))
                        .has_value());
        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("sourav"),
                             string_to_bytes("2026-05"), "age",
                             string_to_bytes("26"))
                        .has_value());

        ASSERT_TRUE(storage_engine.flush().has_value());
    }

    {
        PosixIOEngine engine;
        auto storage_engine_result =
            StorageEngine::open(engine, "./storage_engine_tests", 512);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        ASSERT_EQ(storage_engine.latest_lsn(), 3);

        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("bob"), string_to_bytes("2025-05"),
                             "name", string_to_bytes("Bob the Builder"))
                        .has_value());

        ASSERT_EQ(storage_engine.latest_lsn(), 4);

        /* assert counters */
        auto alice_record = storage_engine
                                .get(string_to_bytes("alice"),
                                     string_to_bytes("2026-05"), "age")
                                .value()
                                .value();
        ASSERT_EQ(alice_record.sequence, 0);
        ASSERT_EQ(bytes_to_string(alice_record.data), "32");

        auto john_record =
            storage_engine
                .get(string_to_bytes("john"), string_to_bytes("2026-05"), "age")
                .value()
                .value();
        ASSERT_EQ(john_record.sequence, 1);
        ASSERT_EQ(bytes_to_string(john_record.data), "30");

        auto sourav_record = storage_engine
                                 .get(string_to_bytes("sourav"),
                                      string_to_bytes("2026-05"), "age")
                                 .value()
                                 .value();
        ASSERT_EQ(sourav_record.sequence, 2);
        ASSERT_EQ(bytes_to_string(sourav_record.data), "26");
    }
}

TEST(StorageEngine, high_water_mark_from_wal_replay) {
    std::string data_dir_path = "./storage_engine_tests";
    Tempdir testdir(data_dir_path);

    {
        PosixIOEngine engine;
        auto storage_engine_result =
            StorageEngine::open(engine, "./storage_engine_tests", 512);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("alice"),
                             string_to_bytes("2026-05"), "age",
                             string_to_bytes("32"))
                        .has_value());
        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("john"),
                             string_to_bytes("2026-05"), "age",
                             string_to_bytes("30"))
                        .has_value());
        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("sourav"),
                             string_to_bytes("2026-05"), "age",
                             string_to_bytes("26"))
                        .has_value());
    }

    {
        PosixIOEngine engine;
        auto storage_engine_result =
            StorageEngine::open(engine, "./storage_engine_tests", 512);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        ASSERT_EQ(storage_engine.latest_lsn(), 3);

        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("bob"), string_to_bytes("2025-05"),
                             "name", string_to_bytes("Bob the Builder"))
                        .has_value());

        ASSERT_EQ(storage_engine.latest_lsn(), 4);

        /* assert counters */
        auto alice_record = storage_engine
                                .get(string_to_bytes("alice"),
                                     string_to_bytes("2026-05"), "age")
                                .value()
                                .value();
        ASSERT_EQ(alice_record.sequence, 0);
        ASSERT_EQ(bytes_to_string(alice_record.data), "32");

        auto john_record =
            storage_engine
                .get(string_to_bytes("john"), string_to_bytes("2026-05"), "age")
                .value()
                .value();
        ASSERT_EQ(john_record.sequence, 1);
        ASSERT_EQ(bytes_to_string(john_record.data), "30");

        auto sourav_record = storage_engine
                                 .get(string_to_bytes("sourav"),
                                      string_to_bytes("2026-05"), "age")
                                 .value()
                                 .value();
        ASSERT_EQ(sourav_record.sequence, 2);
        ASSERT_EQ(bytes_to_string(sourav_record.data), "26");
    }
}

TEST(StorageEngine, high_water_mark_from_sstable_and_wal_replay) {
    std::string data_dir_path = "./storage_engine_tests";
    Tempdir testdir(data_dir_path);

    {
        PosixIOEngine engine;
        auto storage_engine_result =
            StorageEngine::open(engine, "./storage_engine_tests", 512);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("alice"),
                             string_to_bytes("2026-05"), "age",
                             string_to_bytes("32"))
                        .has_value());
        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("john"),
                             string_to_bytes("2026-05"), "age",
                             string_to_bytes("30"))
                        .has_value());
        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("sourav"),
                             string_to_bytes("2026-05"), "age",
                             string_to_bytes("26"))
                        .has_value());

        ASSERT_TRUE(storage_engine.flush().has_value());

        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("gourav"),
                             string_to_bytes("2026-05"), "age",
                             string_to_bytes("24"))
                        .has_value());

        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("tuffy"),
                             string_to_bytes("2026-05"), "age",
                             string_to_bytes("5"))
                        .has_value());
    }

    {
        PosixIOEngine engine;
        auto storage_engine_result =
            StorageEngine::open(engine, "./storage_engine_tests", 512);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        ASSERT_EQ(storage_engine.latest_lsn(), 5);

        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("bob"), string_to_bytes("2025-05"),
                             "name", string_to_bytes("Bob the Builder"))
                        .has_value());

        ASSERT_EQ(storage_engine.latest_lsn(), 6);

        /* assert counters */
        auto alice_record = storage_engine
                                .get(string_to_bytes("alice"),
                                     string_to_bytes("2026-05"), "age")
                                .value()
                                .value();
        ASSERT_EQ(alice_record.sequence, 0);
        ASSERT_EQ(bytes_to_string(alice_record.data), "32");

        auto john_record =
            storage_engine
                .get(string_to_bytes("john"), string_to_bytes("2026-05"), "age")
                .value()
                .value();
        ASSERT_EQ(john_record.sequence, 1);
        ASSERT_EQ(bytes_to_string(john_record.data), "30");

        auto sourav_record = storage_engine
                                 .get(string_to_bytes("sourav"),
                                      string_to_bytes("2026-05"), "age")
                                 .value()
                                 .value();
        ASSERT_EQ(sourav_record.sequence, 2);
        ASSERT_EQ(bytes_to_string(sourav_record.data), "26");

        auto gourav_record = storage_engine
                                 .get(string_to_bytes("gourav"),
                                      string_to_bytes("2026-05"), "age")
                                 .value()
                                 .value();
        ASSERT_EQ(gourav_record.sequence, 3);
        ASSERT_EQ(bytes_to_string(gourav_record.data), "24");

        auto tuffy_record = storage_engine
                                .get(string_to_bytes("tuffy"),
                                     string_to_bytes("2026-05"), "age")
                                .value()
                                .value();
        ASSERT_EQ(tuffy_record.sequence, 4);
        ASSERT_EQ(bytes_to_string(tuffy_record.data), "5");
    }
}

TEST(StorageEngine,
     high_water_mark_from_sstable_and_wal_replay_with_tombstone) {
    std::string data_dir_path = "./storage_engine_tests";
    Tempdir testdir(data_dir_path);

    {
        PosixIOEngine engine;
        auto storage_engine_result =
            StorageEngine::open(engine, "./storage_engine_tests", 512);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("alice"),
                             string_to_bytes("2026-05"), "age",
                             string_to_bytes("32"))
                        .has_value());
        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("john"),
                             string_to_bytes("2026-05"), "age",
                             string_to_bytes("30"))
                        .has_value());
        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("sourav"),
                             string_to_bytes("2026-05"), "age",
                             string_to_bytes("26"))
                        .has_value());

        ASSERT_TRUE(storage_engine.flush().has_value());

        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("gourav"),
                             string_to_bytes("2026-05"), "age",
                             string_to_bytes("24"))
                        .has_value());

        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("tuffy"),
                             string_to_bytes("2026-05"), "age",
                             string_to_bytes("5"))
                        .has_value());

        ASSERT_TRUE(storage_engine
                        .remove(string_to_bytes("tuffy"),
                                string_to_bytes("2026-05"), "age")
                        .has_value());
    }

    {
        PosixIOEngine engine;
        auto storage_engine_result =
            StorageEngine::open(engine, "./storage_engine_tests", 512);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        ASSERT_EQ(storage_engine.latest_lsn(), 6);

        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("bob"), string_to_bytes("2025-05"),
                             "name", string_to_bytes("Bob the Builder"))
                        .has_value());

        ASSERT_EQ(storage_engine.latest_lsn(), 7);

        /* assert counters */
        auto alice_record = storage_engine
                                .get(string_to_bytes("alice"),
                                     string_to_bytes("2026-05"), "age")
                                .value()
                                .value();
        ASSERT_EQ(alice_record.sequence, 0);
        ASSERT_EQ(bytes_to_string(alice_record.data), "32");

        auto john_record =
            storage_engine
                .get(string_to_bytes("john"), string_to_bytes("2026-05"), "age")
                .value()
                .value();
        ASSERT_EQ(john_record.sequence, 1);
        ASSERT_EQ(bytes_to_string(john_record.data), "30");

        auto sourav_record = storage_engine
                                 .get(string_to_bytes("sourav"),
                                      string_to_bytes("2026-05"), "age")
                                 .value()
                                 .value();
        ASSERT_EQ(sourav_record.sequence, 2);
        ASSERT_EQ(bytes_to_string(sourav_record.data), "26");

        auto gourav_record = storage_engine
                                 .get(string_to_bytes("gourav"),
                                      string_to_bytes("2026-05"), "age")
                                 .value()
                                 .value();
        ASSERT_EQ(gourav_record.sequence, 3);
        ASSERT_EQ(bytes_to_string(gourav_record.data), "24");

        auto tuffy_record = storage_engine
                                .get(string_to_bytes("tuffy"),
                                     string_to_bytes("2026-05"), "age")
                                .value()
                                .value();
        ASSERT_EQ(tuffy_record.sequence, 4);
        ASSERT_EQ(bytes_to_string(tuffy_record.data), "5");
    }
}
