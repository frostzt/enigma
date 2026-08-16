#include "enigmadb/storage/dazzle_db/dazzle_engine.h"

#include <filesystem>
#include <memory>
#include <string>

#include "enigmadb/io/posix_io_engine.h"
#include "enigmadb/storage/dazzle_db/compaction/compaction_policy.h"
#include "enigmadb/tempdir.h"
#include "enigmadb/utils.h"
#include "gtest/gtest.h"
#include "test_support/keys.h"

using namespace enigmadb;
using namespace enigmadb::TESTNAMESPACE;

TEST(Dazzle, basic_flow) {
    io::PosixIOEngine engine;
    std::string data_dir_path = "./storage_engine_tests";
    Tempdir testdir(data_dir_path);

    auto storage_engine_result = dazzle::Dazzle::open(engine, data_dir_path, 1024);
    ASSERT_TRUE(storage_engine_result.has_value());

    auto& storage_engine = storage_engine_result.value();
    auto k = make_key("alice", "2026-01", "age");
    ASSERT_TRUE(storage_engine->put(k, string_to_bytes("1234")).has_value());

    auto get_result = storage_engine->get(k);
    ASSERT_TRUE(get_result.has_value());
    ASSERT_TRUE(get_result.value().has_value());

    auto value = get_result.value().value();
    auto value_str = bytes_to_string(value.data);
    ASSERT_EQ(value_str, "1234");
}

TEST(Dazzle, flush_and_read_sstable) {
    io::PosixIOEngine engine;
    std::string data_dir_path = "./storage_engine_tests";
    Tempdir testdir(data_dir_path);

    auto storage_engine_result = dazzle::Dazzle::open(engine, data_dir_path, 512);
    ASSERT_TRUE(storage_engine_result.has_value());

    auto& storage_engine = storage_engine_result.value();

    for (size_t i = 10; i < 160; ++i) {
        auto ki = make_key("alice", "2026-" + std::to_string(i), "age" + std::to_string(i));
        ASSERT_TRUE(storage_engine->put(ki, string_to_bytes("12" + std::to_string(i))).has_value());
    }

    for (size_t i = 10; i < 160; ++i) {
        auto ki = make_key("alice", "2026-" + std::to_string(i), "age" + std::to_string(i));
        auto result = storage_engine->get(ki);

        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(result.value().has_value());

        auto value = result.value().value();
        ASSERT_EQ(bytes_to_string(value.data), "12" + std::to_string(i));
    }
}

TEST(Dazzle, crash_recovery) {
    Tempdir testdir("./storage_engine_tests");

    {
        /* store and crash */
        std::string data_dir_path = "./storage_engine_tests";
        io::PosixIOEngine engine;
        auto storage_engine_result = dazzle::Dazzle::open(engine, data_dir_path, 500000);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        auto key = make_key("alice", "2026-05", "age");
        ASSERT_TRUE(storage_engine->put(key, string_to_bytes("12")).has_value());
    }

    {
        std::string data_dir_path = "./storage_engine_tests";
        io::PosixIOEngine engine;
        auto storage_engine_result = dazzle::Dazzle::open(engine, data_dir_path, 500000);

        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        auto key = make_key("alice", "2026-05", "age");
        auto res = storage_engine->get(key);

        ASSERT_TRUE(res.has_value());
        ASSERT_TRUE(res.value().has_value());

        auto value = res.value().value();
        ASSERT_EQ(bytes_to_string(value.data), "12");
    }
}

TEST(Dazzle, delete_shadowing_across_layers) {
    io::PosixIOEngine engine;
    std::string data_dir_path = "./storage_engine_tests";
    Tempdir testdir(data_dir_path);

    auto storage_engine_result = dazzle::Dazzle::open(engine, data_dir_path, 512);
    ASSERT_TRUE(storage_engine_result.has_value());

    auto& storage_engine = storage_engine_result.value();

    auto key = make_key("alice", "2026-05", "age");
    ASSERT_TRUE(storage_engine->put(key, string_to_bytes("12")).has_value());

    ASSERT_TRUE(storage_engine->flush().has_value());

    auto res = storage_engine->get(key);

    ASSERT_TRUE(res.has_value());
    ASSERT_TRUE(res.value().has_value());

    auto value = res.value().value();
    ASSERT_EQ(bytes_to_string(value.data), "12");

    ASSERT_TRUE(storage_engine->remove(key).has_value());
    ASSERT_TRUE(storage_engine->flush().has_value());

    auto res2 = storage_engine->get(key);

    ASSERT_TRUE(res2.has_value());
    ASSERT_FALSE(res2.value().has_value());
}

TEST(Dazzle, handle_empty_memtable_flush) {
    io::PosixIOEngine engine;
    std::string data_dir_path = "./storage_engine_tests";
    Tempdir testdir(data_dir_path);

    auto storage_engine_result = dazzle::Dazzle::open(engine, data_dir_path, 512);
    ASSERT_TRUE(storage_engine_result.has_value());

    auto& storage_engine = storage_engine_result.value();

    ASSERT_TRUE(storage_engine->flush().has_value());

    std::filesystem::path p = "./storage_engine_tests/sst";

    auto count = std::distance(std::filesystem::directory_iterator(p), std::filesystem::directory_iterator{});
    ASSERT_TRUE(count == 0);
}

TEST(Dazzle, high_water_mark_from_sstable) {
    std::string data_dir_path = "./storage_engine_tests";
    Tempdir testdir(data_dir_path);

    {
        io::PosixIOEngine engine;
        auto storage_engine_result = dazzle::Dazzle::open(engine, "./storage_engine_tests", 512);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        auto k1 = make_key("alice", "2026-05", "age");
        auto k2 = make_key("john", "2026-05", "age");
        auto k3 = make_key("sourav", "2026-05", "age");
        ASSERT_TRUE(storage_engine->put(k1, string_to_bytes("32")).has_value());
        ASSERT_TRUE(storage_engine->put(k2, string_to_bytes("30")).has_value());
        ASSERT_TRUE(storage_engine->put(k3, string_to_bytes("26")).has_value());

        ASSERT_TRUE(storage_engine->flush().has_value());
    }

    {
        io::PosixIOEngine engine;
        auto storage_engine_result = dazzle::Dazzle::open(engine, "./storage_engine_tests", 512);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        ASSERT_EQ(storage_engine->latest_lsn(), 5);

        auto k1 = make_key("bob", "2026-05", "name");
        ASSERT_TRUE(storage_engine->put(k1, string_to_bytes("Bob the Builder")).has_value());

        ASSERT_EQ(storage_engine->latest_lsn(), 6);

        /* assert counters */
        auto k2 = make_key("alice", "2026-05", "age");
        auto k3 = make_key("john", "2026-05", "age");
        auto k4 = make_key("sourav", "2026-05", "age");

        auto alice_record = storage_engine->get_internal(k2).value().value();
        ASSERT_EQ(alice_record.sequence, 1);
        ASSERT_EQ(bytes_to_string(alice_record.data), "32");

        auto john_record = storage_engine->get_internal(k3).value().value();
        ASSERT_EQ(john_record.sequence, 2);
        ASSERT_EQ(bytes_to_string(john_record.data), "30");

        auto sourav_record = storage_engine->get_internal(k4).value().value();
        ASSERT_EQ(sourav_record.sequence, 3);
        ASSERT_EQ(bytes_to_string(sourav_record.data), "26");
    }
}

TEST(Dazzle, high_water_mark_from_wal_replay) {
    std::string data_dir_path = "./storage_engine_tests";
    Tempdir testdir(data_dir_path);

    {
        io::PosixIOEngine engine;
        auto storage_engine_result = dazzle::Dazzle::open(engine, "./storage_engine_tests", 512);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        auto k1 = make_key("alice", "2026-05", "age");
        auto k2 = make_key("john", "2026-05", "age");
        auto k3 = make_key("sourav", "2026-05", "age");

        ASSERT_TRUE(storage_engine->put(k1, string_to_bytes("32")).has_value());
        ASSERT_TRUE(storage_engine->put(k2, string_to_bytes("30")).has_value());
        ASSERT_TRUE(storage_engine->put(k3, string_to_bytes("26")).has_value());
    }

    {
        io::PosixIOEngine engine;
        auto storage_engine_result = dazzle::Dazzle::open(engine, "./storage_engine_tests", 512);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        ASSERT_EQ(storage_engine->latest_lsn(), 4);

        auto kb = make_key("bob", "2025-05", "name");
        auto k1 = make_key("alice", "2026-05", "age");
        auto k2 = make_key("john", "2026-05", "age");
        auto k3 = make_key("sourav", "2026-05", "age");

        ASSERT_TRUE(storage_engine->put(kb, string_to_bytes("Bob the Builder")).has_value());

        ASSERT_EQ(storage_engine->latest_lsn(), 5);

        /* assert counters */
        auto alice_record = storage_engine->get_internal(k1).value().value();
        ASSERT_EQ(alice_record.sequence, 1);
        ASSERT_EQ(bytes_to_string(alice_record.data), "32");

        auto john_record = storage_engine->get_internal(k2).value().value();
        ASSERT_EQ(john_record.sequence, 2);
        ASSERT_EQ(bytes_to_string(john_record.data), "30");

        auto sourav_record = storage_engine->get_internal(k3).value().value();
        ASSERT_EQ(sourav_record.sequence, 3);
        ASSERT_EQ(bytes_to_string(sourav_record.data), "26");
    }
}

TEST(Dazzle, high_water_mark_from_sstable_and_wal_replay) {
    std::string data_dir_path = "./storage_engine_tests";
    Tempdir testdir(data_dir_path);

    {
        io::PosixIOEngine engine;
        auto storage_engine_result = dazzle::Dazzle::open(engine, "./storage_engine_tests", 512);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        auto k1 = make_key("alice", "2026-05", "age");
        auto k2 = make_key("john", "2026-05", "age");
        auto k3 = make_key("sourav", "2026-05", "age");
        auto k4 = make_key("gourav", "2026-05", "age");
        auto k5 = make_key("tuffy", "2026-05", "age");

        ASSERT_TRUE(storage_engine->put(k1, string_to_bytes("32")).has_value());
        ASSERT_TRUE(storage_engine->put(k2, string_to_bytes("30")).has_value());
        ASSERT_TRUE(storage_engine->put(k3, string_to_bytes("26")).has_value());

        ASSERT_TRUE(storage_engine->flush().has_value());

        ASSERT_TRUE(storage_engine->put(k4, string_to_bytes("24")).has_value());
        ASSERT_TRUE(storage_engine->put(k5, string_to_bytes("5")).has_value());
    }

    {
        io::PosixIOEngine engine;
        auto storage_engine_result = dazzle::Dazzle::open(engine, "./storage_engine_tests", 512);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();

        ASSERT_EQ(storage_engine->latest_lsn(), 6);

        auto kb = make_key("bob", "2025-05", "name");
        auto k1 = make_key("alice", "2026-05", "age");
        auto k2 = make_key("john", "2026-05", "age");
        auto k3 = make_key("sourav", "2026-05", "age");
        auto k4 = make_key("gourav", "2026-05", "age");
        auto k5 = make_key("tuffy", "2026-05", "age");

        ASSERT_TRUE(storage_engine->put(kb, string_to_bytes("Bob the Builder")).has_value());

        ASSERT_EQ(storage_engine->latest_lsn(), 7);

        /* assert counters */
        auto alice_record = storage_engine->get_internal(k1).value().value();
        ASSERT_EQ(alice_record.sequence, 1);
        ASSERT_EQ(bytes_to_string(alice_record.data), "32");

        auto john_record = storage_engine->get_internal(k2).value().value();
        ASSERT_EQ(john_record.sequence, 2);
        ASSERT_EQ(bytes_to_string(john_record.data), "30");

        auto sourav_record = storage_engine->get_internal(k3).value().value();
        ASSERT_EQ(sourav_record.sequence, 3);
        ASSERT_EQ(bytes_to_string(sourav_record.data), "26");

        auto gourav_record = storage_engine->get_internal(k4).value().value();
        ASSERT_EQ(gourav_record.sequence, 4);
        ASSERT_EQ(bytes_to_string(gourav_record.data), "24");

        auto tuffy_record = storage_engine->get_internal(k5).value().value();
        ASSERT_EQ(tuffy_record.sequence, 5);
        ASSERT_EQ(bytes_to_string(tuffy_record.data), "5");
    }
}

TEST(Dazzle, high_water_mark_from_sstable_and_wal_replay_with_tombstone) {
    std::string data_dir_path = "./storage_engine_tests";
    Tempdir testdir(data_dir_path);

    {
        io::PosixIOEngine engine;
        auto storage_engine_result = dazzle::Dazzle::open(engine, "./storage_engine_tests", 512);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();
        ASSERT_TRUE(storage_engine->set_compaction_policy(std::make_unique<dazzle::SizeTieredCompactionPolicy>(16, 20))
                        .has_value());

        auto k1 = make_key("alice", "2026-05", "age");
        auto k2 = make_key("john", "2026-05", "age");
        auto k3 = make_key("sourav", "2026-05", "age");
        auto k4 = make_key("gourav", "2026-05", "age");
        auto k5 = make_key("tuffy", "2026-05", "age");

        ASSERT_TRUE(storage_engine->put(k1, string_to_bytes("32")).has_value());
        ASSERT_TRUE(storage_engine->put(k2, string_to_bytes("30")).has_value());
        ASSERT_TRUE(storage_engine->put(k3, string_to_bytes("26")).has_value());

        ASSERT_TRUE(storage_engine->flush().has_value());

        ASSERT_TRUE(storage_engine->put(k4, string_to_bytes("24")).has_value());
        ASSERT_TRUE(storage_engine->put(k5, string_to_bytes("5")).has_value());
        ASSERT_TRUE(storage_engine->remove(k5).has_value());
    }

    {
        io::PosixIOEngine engine;
        auto storage_engine_result = dazzle::Dazzle::open(engine, "./storage_engine_tests", 512);
        ASSERT_TRUE(storage_engine_result.has_value());

        auto& storage_engine = storage_engine_result.value();
        ASSERT_TRUE(storage_engine->set_compaction_policy(std::make_unique<dazzle::SizeTieredCompactionPolicy>(16, 20))
                        .has_value());
        ASSERT_EQ(storage_engine->latest_lsn(), 7);

        auto kb = make_key("bob", "2025-05", "name");
        auto k1 = make_key("alice", "2026-05", "age");
        auto k2 = make_key("john", "2026-05", "age");
        auto k3 = make_key("sourav", "2026-05", "age");
        auto k4 = make_key("gourav", "2026-05", "age");
        auto k5 = make_key("tuffy", "2026-05", "age");

        ASSERT_TRUE(storage_engine->put(kb, string_to_bytes("Bob the Builder")).has_value());

        ASSERT_EQ(storage_engine->latest_lsn(), 8);

        /* assert counters */
        auto alice_record = storage_engine->get_internal(k1).value().value();
        ASSERT_EQ(alice_record.sequence, 1);
        ASSERT_EQ(bytes_to_string(alice_record.data), "32");

        auto john_record = storage_engine->get_internal(k2).value().value();
        ASSERT_EQ(john_record.sequence, 2);
        ASSERT_EQ(bytes_to_string(john_record.data), "30");

        auto sourav_record = storage_engine->get_internal(k3).value().value();
        ASSERT_EQ(sourav_record.sequence, 3);
        ASSERT_EQ(bytes_to_string(sourav_record.data), "26");

        auto gourav_record = storage_engine->get_internal(k4).value().value();
        ASSERT_EQ(gourav_record.sequence, 4);
        ASSERT_EQ(bytes_to_string(gourav_record.data), "24");

        auto tuffy_record = storage_engine->get_internal(k5).value().value();
        ASSERT_EQ(tuffy_record.sequence, 6);
        ASSERT_EQ(bytes_to_string(tuffy_record.data), "");
    }
}
