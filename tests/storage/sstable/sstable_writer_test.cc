#include "enigmadb/storage/sstable/sstable_writer.h"

#include <filesystem>
#include <string>

#include "enigmadb/common/tempfile.h"
#include "enigmadb/common/utils.h"
#include "enigmadb/io/posix_io_engine.h"
#include "enigmadb/storage/key_encoding.h"
#include "enigmadb/storage/memtable/memtable.h"
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

    auto crewriter_result = SSTableWriter::create(engine, testfile.path, 5096);
    ASSERT_TRUE(crewriter_result.has_value());

    auto& writer = crewriter_result.value();

    ASSERT_TRUE(
        writer
            .add(encode_composite_key(string_to_bytes("alice"),
                                      string_to_bytes("2026-01"), "age"),
                 memtable::MemtableValue{string_to_bytes("30"), false})
            .has_value());

    ASSERT_TRUE(
        writer
            .add(encode_composite_key(string_to_bytes("alice"),
                                      string_to_bytes("2026-01"), "name"),
                 memtable::MemtableValue{string_to_bytes("Alice"), false})
            .has_value());

    ASSERT_TRUE(
        writer
            .add(encode_composite_key(string_to_bytes("bob"),
                                      string_to_bytes("2026-01"), "name"),
                 memtable::MemtableValue{string_to_bytes("Bob"), false})
            .has_value());

    auto finish_result = writer.finish();
    ASSERT_TRUE(finish_result.has_value());

    auto curr_size = std::filesystem::file_size(testfile.path);
    ASSERT_TRUE(curr_size > prev_size);
    /* FIXME: Assertion based on manual calculation (approx) replace once we
     * build the reader */
    ASSERT_TRUE(curr_size > 55);
}
