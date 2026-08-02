#include "enigmadb/storage/dazzle_db/sstable/sstable_iterator.h"

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

TEST(sstable_iterator, empty_sstable) {
    io::PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto crewriter_result =
        dazzle::SSTableWriter::create(engine, testfile.path, 250);
    ASSERT_TRUE(crewriter_result.has_value());

    auto& writer = crewriter_result.value();
    auto finish_result = writer.finish();
    ASSERT_TRUE(finish_result.has_value());

    auto crewreader_result =
        dazzle::SSTableReader::create(engine, testfile.path);
    ASSERT_TRUE(crewreader_result.has_value());

    auto& reader = crewreader_result.value();
    auto itr = reader.iterator();

    itr.seek_to_first();

    auto status = itr.status();
    ASSERT_TRUE(status.has_value());
    ASSERT_FALSE(itr.valid());
}

TEST(sstable_iterator, simple_loop) {
    io::PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto prev_size = std::filesystem::file_size(testfile.path);

    auto crewriter_result =
        dazzle::SSTableWriter::create(engine, testfile.path, 250);
    ASSERT_TRUE(crewriter_result.has_value());

    auto& writer = crewriter_result.value();

    for (size_t i = 10; i < 60; i++) {
        auto k = make_key("user:" + std::to_string(i), "2026-01", "age");
        auto v = dazzle::InternalValue{
            string_to_bytes("value_" + std::to_string(i)), false, i};
        ASSERT_TRUE(writer.add(k, v).has_value());
    }

    auto finish_result = writer.finish();
    ASSERT_TRUE(finish_result.has_value());

    auto curr_size = std::filesystem::file_size(testfile.path);
    ASSERT_TRUE(curr_size > prev_size);

    auto crewreader_result =
        dazzle::SSTableReader::create(engine, testfile.path);
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
    io::PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto crewriter_result =
        dazzle::SSTableWriter::create(engine, testfile.path, 250);
    ASSERT_TRUE(crewriter_result.has_value());

    auto& writer = crewriter_result.value();

    auto k = make_key("user:sourav", "2026-01", "age");
    auto v = dazzle::InternalValue{string_to_bytes("value_123"), false, 1};
    ASSERT_TRUE(writer.add(k, v).has_value());

    auto finish_result = writer.finish();
    ASSERT_TRUE(finish_result.has_value());

    auto crewreader_result =
        dazzle::SSTableReader::create(engine, testfile.path);
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
    io::PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto crewriter_result =
        dazzle::SSTableWriter::create(engine, testfile.path, 50001);
    ASSERT_TRUE(crewriter_result.has_value());

    auto& writer = crewriter_result.value();

    for (size_t i = 10; i < 50000; i++) {
        auto k = make_key("user:" + std::to_string(i), "2026-01", "age");
        auto v = dazzle::InternalValue{
            string_to_bytes("value_" + std::to_string(i)), false, i};
        ASSERT_TRUE(writer.add(k, v).has_value());
    }

    auto finish_result = writer.finish();
    ASSERT_TRUE(finish_result.has_value());

    auto crewreader_result =
        dazzle::SSTableReader::create(engine, testfile.path);
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
