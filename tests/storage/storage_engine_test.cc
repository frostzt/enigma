#include "enigmadb/storage/storage_engine.h"

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

    for (size_t i = 10; i < 60; ++i) {
        ASSERT_TRUE(storage_engine
                        .put(string_to_bytes("alice"),
                             string_to_bytes("2026-" + std::to_string(i)),
                             "age" + std::to_string(i),
                             string_to_bytes("12" + std::to_string(i)))
                        .has_value());
    }

    for (size_t i = 10; i < 60; ++i) {
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
