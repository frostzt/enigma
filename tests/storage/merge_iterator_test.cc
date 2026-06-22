#include "enigmadb/storage/merge_iterator.h"

#include "enigmadb/common/tempdir.h"
#include "enigmadb/common/utils.h"
#include "enigmadb/io/posix_io_engine.h"
#include "enigmadb/storage/fake_iterator.h"
#include "enigmadb/storage/key_encoding.h"
#include "enigmadb/storage/sstable/sstable_reader.h"
#include "enigmadb/storage/storage_engine.h"
#include "gtest/gtest.h"

using namespace enigmadb::io;
using namespace enigmadb::common;
using namespace enigmadb::storage;
using namespace enigmadb::storage::sstable;
using namespace enigmadb::storage::memtable;

TEST(merge_iterator, same_key_in_two_sources) {
    PosixIOEngine engine;

    std::string data_dir_path = "./storage_engine_tests";
    Tempdir testdir(data_dir_path);

    auto storage_engine_result =
        StorageEngine::open(engine, data_dir_path, 1024);
    ASSERT_TRUE(storage_engine_result.has_value());

    auto& storage_engine = storage_engine_result.value();

    /* create first sstable */
    ASSERT_TRUE(storage_engine
                    .put(string_to_bytes("alice"), string_to_bytes("2026-01"),
                         "age", string_to_bytes("1234"))
                    .has_value());

    ASSERT_TRUE(storage_engine.flush().has_value());

    /* create second sstable */
    ASSERT_TRUE(storage_engine
                    .put(string_to_bytes("alice"), string_to_bytes("2026-01"),
                         "age", string_to_bytes("1234"))
                    .has_value());

    ASSERT_TRUE(storage_engine.flush().has_value());

    /* read sstables and create individual readers */
    auto r1 = SSTableReader::create(
        engine, "./storage_engine_tests/sst/sst_00000001.db");
    ASSERT_TRUE(r1.has_value());

    auto r2 = SSTableReader::create(
        engine, "./storage_engine_tests/sst/sst_00000002.db");
    ASSERT_TRUE(r2.has_value());

    auto& reader1 = r1.value();
    auto& reader2 = r2.value();

    auto itr1 = reader1.iterator();
    itr1.seek_to_first();

    auto itr2 = reader2.iterator();
    itr2.seek_to_first();

    auto he1 = HeapEntry{&itr1};
    auto he2 = HeapEntry{&itr2};

    HeapCompare cmp;
    ASSERT_TRUE(cmp(he1, he2));
}

TEST(merge_iterator, iterator_compare) {
    FakeIterator older(
        encode_composite_key(string_to_bytes("partition"),
                             string_to_bytes("cluster"), "column"),
        MemtableValue{string_to_bytes("value"), false, 1});
    FakeIterator newer(
        encode_composite_key(string_to_bytes("partition"),
                             string_to_bytes("cluster"), "column"),
        MemtableValue{string_to_bytes("value"), false, 2});

    HeapCompare cmp;

    EXPECT_TRUE(cmp(HeapEntry{&older}, HeapEntry{&newer}));
    EXPECT_FALSE(cmp(HeapEntry{&newer}, HeapEntry{&older}));
}
