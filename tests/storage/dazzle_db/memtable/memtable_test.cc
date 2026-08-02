#include "enigmadb/storage/dazzle_db/memtable/memtable.h"

#include <string>

#include "enigmadb/utils.h"
#include "gtest/gtest.h"
#include "test_support/keys.h"

using namespace enigmadb;
using namespace enigmadb::TESTNAMESPACE;

TEST(MemTable, put_then_get) {
    dazzle::Memtable memtable(1024);

    std::string partition_key = "user:123";
    std::string clustering_key = "2000-01-06";

    /* add a bunch of records */
    auto k1 = make_key(partition_key, clustering_key, "age");
    memtable.put(k1, as_bytes("26"), 1);
    auto k2 = make_key(partition_key, clustering_key, "name");
    memtable.put(k2, as_bytes("sourav"), 2);

    /* fetch these records */
    auto age = memtable.get(k1);
    ASSERT_EQ(bytes_to_string(age.value().data), "26");
    auto name = memtable.get(k2);
    ASSERT_EQ(bytes_to_string(name.value().data), "sourav");
}

TEST(MemTable, put_remove_get) {
    dazzle::Memtable memtable(1024);

    std::string partition_key = "user:123";
    std::string clustering_key = "2000-01-06";

    /* add a record */
    auto k1 = make_key(partition_key, clustering_key, "name");
    memtable.put(k1, as_bytes("sourav"), 1);

    /* remove that record */
    memtable.remove(k1, 2);

    /* fetch record */
    auto name = memtable.get(k1);
    ASSERT_TRUE(name.has_value());
    ASSERT_TRUE(name.value().is_tombstone);
}

TEST(MemTable, remove_non_existant) {
    dazzle::Memtable memtable(1024);

    std::string partition_key = "user:123";
    std::string clustering_key = "2000-01-06";

    /* remove a record */
    auto k1 = make_key(partition_key, clustering_key, "name");
    memtable.remove(k1, 1);

    /* fetch record */
    auto name = memtable.get(k1);
    ASSERT_TRUE(name.value().is_tombstone);
    ASSERT_GT(memtable.approximate_size(), 0);
    ASSERT_EQ(memtable.count(), 1);
}

TEST(MemTable, value_overwrites) {
    dazzle::Memtable memtable(1024);

    std::string partition_key = "user:123";
    std::string clustering_key = "2000-01-06";

    /* add a bunch of records */
    auto k1 = make_key(partition_key, clustering_key, "name");
    memtable.put(k1, as_bytes("artyom"), 1);

    /* fetch these records */
    auto artyom = memtable.get(k1);
    ASSERT_EQ(bytes_to_string(artyom.value().data), "artyom");

    /* overwrite these recods */
    memtable.put(k1, as_bytes("anna"), 2);

    /* fetch these records */
    auto nowanna = memtable.get(k1);
    ASSERT_EQ(bytes_to_string(nowanna.value().data), "anna");
}

TEST(MemTable, flush_uses_count) {
    dazzle::Memtable memtable(50);

    std::string partition_key = "user:123";
    std::string clustering_key = "2000-01-06";

    /* add a bunch of records */
    auto k1 = make_key(partition_key, clustering_key, "name");
    memtable.put(k1, as_bytes("artyom"), 1);

    /* fetch these records */
    auto artyom = memtable.get(k1);
    ASSERT_EQ(bytes_to_string(artyom.value().data), "artyom");

    ASSERT_FALSE(memtable.should_flush());

    for (size_t i = 0; i < 5; i++) {
        auto ki = make_key("random_" + std::to_string(i),
                           "random_clus_" + std::to_string(i), "name");
        memtable.put(ki, as_bytes("artyom"), 2);
    }

    ASSERT_TRUE(memtable.should_flush());
}
