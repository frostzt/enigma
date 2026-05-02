#include <filesystem>
#include <string>

#include "enigmadb/common/tempfile.h"
#include "enigmadb/common/utils.h"
#include "enigmadb/io/posix_io_engine.h"
#include "enigmadb/storage/key_encoding.h"
#include "enigmadb/storage/memtable/memtable.h"
#include "enigmadb/storage/sstable/sstable_reader.h"
#include "enigmadb/storage/sstable/sstable_writer.h"
#include "gtest/gtest.h"

using namespace enigmadb::io;
using namespace enigmadb::common;
using namespace enigmadb::storage;
using namespace enigmadb::storage::sstable;
using namespace enigmadb::storage::memtable;

TEST(SSTableWriter, add_finish_verify) {
    PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto prev_size = std::filesystem::file_size(testfile.path);

    /* write flow */
    auto crewriter_result = SSTableWriter::create(engine, testfile.path);
    ASSERT_TRUE(crewriter_result.has_value());

    auto& writer = crewriter_result.value();

    auto alice_age_key = encode_composite_key(
        string_to_bytes("alice"), string_to_bytes("2026-01"), "age");
    auto alice_name_key = encode_composite_key(
        string_to_bytes("alice"), string_to_bytes("2026-01"), "name");
    auto bob_name_key = encode_composite_key(
        string_to_bytes("bob"), string_to_bytes("2026-01"), "name");

    ASSERT_TRUE(writer
                    .add(alice_age_key,
                         memtable::MemtableValue{string_to_bytes("30"), false})
                    .has_value());
    ASSERT_TRUE(
        writer
            .add(alice_name_key,
                 memtable::MemtableValue{string_to_bytes("Alice"), false})
            .has_value());

    ASSERT_TRUE(writer
                    .add(bob_name_key,
                         memtable::MemtableValue{string_to_bytes("Bob"), false})
                    .has_value());

    auto finish_result = writer.finish();
    ASSERT_TRUE(finish_result.has_value());

    auto curr_size = std::filesystem::file_size(testfile.path);
    ASSERT_TRUE(curr_size > prev_size);

    /* read flow */
    auto crewreader_result = SSTableReader::create(engine, testfile.path);
    ASSERT_TRUE(crewreader_result.has_value());

    auto& reader = crewreader_result.value();

    auto alice_age_result = reader.get(alice_age_key);
    ASSERT_TRUE(alice_age_result.has_value());
    auto alice_age_opt = alice_age_result.value();
    ASSERT_TRUE(alice_age_opt.has_value());
    auto alice_age = alice_age_opt.value();
    ASSERT_EQ(bytes_to_string(alice_age.data), "30");

    auto alice_name_result = reader.get(alice_name_key);
    ASSERT_TRUE(alice_name_result.has_value());
    auto alice_name_opt = alice_name_result.value();
    ASSERT_TRUE(alice_name_opt.has_value());
    auto alice_name = alice_name_opt.value();
    ASSERT_EQ(bytes_to_string(alice_name.data), "Alice");

    auto bob_name_result = reader.get(bob_name_key);
    ASSERT_TRUE(bob_name_result.has_value());
    auto bob_name_opt = bob_name_result.value();
    ASSERT_TRUE(bob_name_opt.has_value());
    auto bob_name = bob_name_opt.value();
    ASSERT_EQ(bytes_to_string(bob_name.data), "Bob");
}

TEST(SSTableWriter, single_entry_finish) {
    PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto prev_size = std::filesystem::file_size(testfile.path);

    auto crewriter_result = SSTableWriter::create(engine, testfile.path);
    ASSERT_TRUE(crewriter_result.has_value());

    auto& writer = crewriter_result.value();
    auto key = encode_composite_key(string_to_bytes("alice"),
                                    string_to_bytes("2026-01"), "age");

    ASSERT_TRUE(
        writer.add(key, memtable::MemtableValue{string_to_bytes("30"), false})
            .has_value());

    auto finish_result = writer.finish();
    ASSERT_TRUE(finish_result.has_value());

    auto curr_size = std::filesystem::file_size(testfile.path);
    ASSERT_TRUE(curr_size > prev_size);

    auto crewreader_result = SSTableReader::create(engine, testfile.path);
    ASSERT_TRUE(crewreader_result.has_value());

    auto& reader = crewreader_result.value();

    auto read_result = reader.get(key);
    ASSERT_TRUE(read_result.has_value());
    ASSERT_EQ(bytes_to_string(read_result.value().value().data), "30");
}

TEST(SSTableWriter, multiple_records) {
    PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto prev_size = std::filesystem::file_size(testfile.path);

    auto crewriter_result = SSTableWriter::create(engine, testfile.path);
    ASSERT_TRUE(crewriter_result.has_value());

    auto& writer = crewriter_result.value();

    for (size_t i = 10; i < 60; i++) {
        ASSERT_TRUE(
            writer
                .add(encode_composite_key(
                         string_to_bytes("user:" + std::to_string(i)),
                         string_to_bytes("2026-01"), "age"),
                     memtable::MemtableValue{
                         string_to_bytes("value_" + std::to_string(i)), false})
                .has_value());
    }

    auto finish_result = writer.finish();
    ASSERT_TRUE(finish_result.has_value());

    auto curr_size = std::filesystem::file_size(testfile.path);
    ASSERT_TRUE(curr_size > prev_size);

    auto crewreader_result = SSTableReader::create(engine, testfile.path);
    ASSERT_TRUE(crewreader_result.has_value());

    auto& reader = crewreader_result.value();

    for (size_t i = 10; i < 60; i++) {
        auto read_res = reader.get(
            encode_composite_key(string_to_bytes("user:" + std::to_string(i)),
                                 string_to_bytes("2026-01"), "age"));
        ASSERT_TRUE(read_res.has_value());
        ASSERT_EQ(bytes_to_string(read_res.value().value().data),
                  "value_" + std::to_string(i));
    }
}

TEST(SSTableWriter, tombstone_record) {
    PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto crewriter_result = SSTableWriter::create(engine, testfile.path);
    ASSERT_TRUE(crewriter_result.has_value());

    auto& writer = crewriter_result.value();

    ASSERT_TRUE(
        writer
            .add(encode_composite_key(string_to_bytes("alice"),
                                      string_to_bytes("2026-01"), "age"),
                 memtable::MemtableValue{string_to_bytes("30"), true})
            .has_value());

    auto finish_result = writer.finish();
    ASSERT_TRUE(finish_result.has_value());

    auto crewreader_result = SSTableReader::create(engine, testfile.path);
    ASSERT_TRUE(crewreader_result.has_value());

    auto& reader = crewreader_result.value();
    auto read_res = reader.get(encode_composite_key(
        string_to_bytes("alice"), string_to_bytes("2026-01"), "age"));
    ASSERT_TRUE(read_res.has_value());
    auto val = read_res.value().value();
    ASSERT_TRUE(val.is_tombstone);
}
