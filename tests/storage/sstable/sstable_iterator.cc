#include "enigmadb/storage/sstable/sstable_iterator.h"

#include <filesystem>
#include <string>

#include "enigmadb/common/tempfile.h"
#include "enigmadb/common/utils.h"
#include "enigmadb/io/posix_io_engine.h"
#include "enigmadb/storage/sstable/sstable_reader.h"
#include "enigmadb/storage/sstable/sstable_writer.h"
#include "gtest/gtest.h"

using namespace enigmadb::io;
using namespace enigmadb::common;
using namespace enigmadb::storage;
using namespace enigmadb::storage::sstable;
using namespace enigmadb::storage::memtable;

TEST(sstable_iterator, empty_sstable) {
    PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto crewriter_result = SSTableWriter::create(engine, testfile.path, 250);
    ASSERT_TRUE(crewriter_result.has_value());

    auto& writer = crewriter_result.value();
    auto finish_result = writer.finish();
    ASSERT_TRUE(finish_result.has_value());

    auto crewreader_result = SSTableReader::create(engine, testfile.path);
    ASSERT_TRUE(crewreader_result.has_value());

    auto& reader = crewreader_result.value();
    auto itr = reader.iterator();

    itr.seek_to_first();

    auto status = itr.status();
    ASSERT_TRUE(status.has_value());
    ASSERT_FALSE(itr.valid());
}

TEST(sstable_iterator, simple_loop) {
    PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto prev_size = std::filesystem::file_size(testfile.path);

    auto crewriter_result = SSTableWriter::create(engine, testfile.path, 250);
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
    auto itr = reader.iterator();

    size_t chk_itr = 10;
    for (itr.seek_to_first(); itr.valid(); itr.next()) {
        auto v = itr.value();
        ASSERT_EQ("value_" + std::to_string(chk_itr), bytes_to_string(v.data));
        chk_itr++;
    }
}

TEST(sstable_iterator, exhaustion) {
    PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto crewriter_result = SSTableWriter::create(engine, testfile.path, 250);
    ASSERT_TRUE(crewriter_result.has_value());

    auto& writer = crewriter_result.value();

    ASSERT_TRUE(
        writer
            .add(encode_composite_key(string_to_bytes("user:sourav"),
                                      string_to_bytes("2026-01"), "age"),
                 memtable::MemtableValue{string_to_bytes("value_123"), false})
            .has_value());

    auto finish_result = writer.finish();
    ASSERT_TRUE(finish_result.has_value());

    auto crewreader_result = SSTableReader::create(engine, testfile.path);
    ASSERT_TRUE(crewreader_result.has_value());

    auto& reader = crewreader_result.value();
    auto itr = reader.iterator();

    itr.seek_to_first();

    ASSERT_EQ("value_123", bytes_to_string(itr.value().data));

    auto status = itr.status();
    ASSERT_TRUE(status.has_value());
    ASSERT_TRUE(itr.valid());

    itr.next();
    status = itr.status();
    ASSERT_TRUE(status.has_value());
    ASSERT_FALSE(itr.valid());
}

TEST(sstable_iterator, large_key_value_pairs) {
    PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto crewriter_result = SSTableWriter::create(engine, testfile.path, 50001);
    ASSERT_TRUE(crewriter_result.has_value());

    auto& writer = crewriter_result.value();

    for (size_t i = 10; i < 50000; i++) {
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

    auto crewreader_result = SSTableReader::create(engine, testfile.path);
    ASSERT_TRUE(crewreader_result.has_value());

    auto& reader = crewreader_result.value();
    auto itr = reader.iterator();

    size_t chk_itr = 10;
    for (itr.seek_to_first(); itr.valid(); itr.next()) {
        auto v = itr.value();
        ASSERT_EQ("value_" + std::to_string(chk_itr), bytes_to_string(v.data));
        chk_itr++;
    }
}
