#include <filesystem>
#include <string>

#include "enigmadb/io/posix_io_engine.h"
#include "enigmadb/storage/dazzle_db/internal_value.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_reader.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_writer.h"
#include "enigmadb/tempfile.h"
#include "enigmadb/utils.h"
#include "gtest/gtest.h"
#include "test_support/keys.h"

using namespace enigmadb;
using namespace enigmadb::TESTNAMESPACE;

TEST(SSTableWriter, add_finish_verify) {
    io::PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto prev_size = std::filesystem::file_size(testfile.path);

    /* write flow */
    auto crewriter_result =
        dazzle::SSTableWriter::create(engine, testfile.path, 50);
    ASSERT_TRUE(crewriter_result.has_value());

    auto& writer = crewriter_result.value();

    auto k1 = make_key("alice", "2026-01", "age");
    auto v1 = dazzle::InternalValue{string_to_bytes("30"), false, 1};
    auto k2 = make_key("alice", "2026-01", "name");
    auto v2 = dazzle::InternalValue{string_to_bytes("Alice"), false, 2};
    auto k3 = make_key("bob", "2026-01", "name");
    auto v3 = dazzle::InternalValue{string_to_bytes("Bob"), false, 3};

    ASSERT_TRUE(writer.add(k1, v1).has_value());
    ASSERT_TRUE(writer.add(k2, v2).has_value());
    ASSERT_TRUE(writer.add(k3, v3).has_value());

    auto finish_result = writer.finish();
    ASSERT_TRUE(finish_result.has_value());

    auto curr_size = std::filesystem::file_size(testfile.path);
    ASSERT_TRUE(curr_size > prev_size);

    /* read flow */
    auto crewreader_result =
        dazzle::SSTableReader::create(engine, testfile.path);
    ASSERT_TRUE(crewreader_result.has_value());

    auto& reader = crewreader_result.value();

    auto alice_age_result = reader.get(k1);
    ASSERT_TRUE(alice_age_result.has_value());
    auto alice_age_opt = alice_age_result.value();
    ASSERT_TRUE(alice_age_opt.has_value());
    auto alice_age = alice_age_opt.value();
    ASSERT_EQ(bytes_to_string(alice_age.data), "30");

    auto alice_name_result = reader.get(k2);
    ASSERT_TRUE(alice_name_result.has_value());
    auto alice_name_opt = alice_name_result.value();
    ASSERT_TRUE(alice_name_opt.has_value());
    auto alice_name = alice_name_opt.value();
    ASSERT_EQ(bytes_to_string(alice_name.data), "Alice");

    auto bob_name_result = reader.get(k3);
    ASSERT_TRUE(bob_name_result.has_value());
    auto bob_name_opt = bob_name_result.value();
    ASSERT_TRUE(bob_name_opt.has_value());
    auto bob_name = bob_name_opt.value();
    ASSERT_EQ(bytes_to_string(bob_name.data), "Bob");
}

TEST(SSTableWriter, single_entry_finish) {
    io::PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto prev_size = std::filesystem::file_size(testfile.path);

    auto crewriter_result =
        dazzle::SSTableWriter::create(engine, testfile.path, 50);
    ASSERT_TRUE(crewriter_result.has_value());

    auto& writer = crewriter_result.value();
    auto key = make_key("alice", "2026-01", "age");
    auto value = dazzle::InternalValue{string_to_bytes("30"), false, 1};

    ASSERT_TRUE(writer.add(key, value).has_value());

    auto finish_result = writer.finish();
    ASSERT_TRUE(finish_result.has_value());

    auto curr_size = std::filesystem::file_size(testfile.path);
    ASSERT_TRUE(curr_size > prev_size);

    auto crewreader_result =
        dazzle::SSTableReader::create(engine, testfile.path);
    ASSERT_TRUE(crewreader_result.has_value());

    auto& reader = crewreader_result.value();

    auto read_result = reader.get(key);
    ASSERT_TRUE(read_result.has_value());
    ASSERT_EQ(bytes_to_string(read_result.value().value().data), "30");
}

TEST(SSTableWriter, multiple_records) {
    io::PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto prev_size = std::filesystem::file_size(testfile.path);

    auto crewriter_result =
        dazzle::SSTableWriter::create(engine, testfile.path, 250);
    ASSERT_TRUE(crewriter_result.has_value());

    auto& writer = crewriter_result.value();

    for (size_t i = 10; i < 60; i++) {
        auto ki = make_key("user:" + std::to_string(i), "2026-01", "age");
        auto vi = dazzle::InternalValue{
            string_to_bytes("value_" + std::to_string(i)), false, i};
        ASSERT_TRUE(writer.add(ki, vi).has_value());
    }

    auto finish_result = writer.finish();
    ASSERT_TRUE(finish_result.has_value());

    auto curr_size = std::filesystem::file_size(testfile.path);
    ASSERT_TRUE(curr_size > prev_size);

    auto crewreader_result =
        dazzle::SSTableReader::create(engine, testfile.path);
    ASSERT_TRUE(crewreader_result.has_value());

    auto& reader = crewreader_result.value();

    for (size_t i = 10; i < 60; i++) {
        auto ki = make_key("user:" + std::to_string(i), "2026-01", "age");
        auto read_res = reader.get(ki);
        ASSERT_TRUE(read_res.has_value());
        ASSERT_EQ(bytes_to_string(read_res.value().value().data),
                  "value_" + std::to_string(i));
    }
}

TEST(SSTableWriter, tombstone_record) {
    io::PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto crewriter_result =
        dazzle::SSTableWriter::create(engine, testfile.path, 10);
    ASSERT_TRUE(crewriter_result.has_value());

    auto& writer = crewriter_result.value();
    auto k = make_key("alice", "2026-01", "age");
    auto v = dazzle::InternalValue{string_to_bytes("30"), true, 1};

    ASSERT_TRUE(writer.add(k, v).has_value());

    auto finish_result = writer.finish();
    ASSERT_TRUE(finish_result.has_value());

    auto crewreader_result =
        dazzle::SSTableReader::create(engine, testfile.path);
    ASSERT_TRUE(crewreader_result.has_value());

    auto& reader = crewreader_result.value();
    auto read_res = reader.get(k);
    ASSERT_TRUE(read_res.has_value());
    auto val = read_res.value().value();
    ASSERT_TRUE(val.is_tombstone);
}
