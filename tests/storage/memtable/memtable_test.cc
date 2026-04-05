#include "enigmadb/storage/memtable/memtable.h"

#include <string>

#include "enigmadb/common/utils.h"
#include "gtest/gtest.h"

using namespace enigmadb::common;
using namespace enigmadb::storage;
using namespace enigmadb::storage::memtable;

TEST(MemTable, put_then_get) {
    Memtable memtable(1024);

    std::string partition_key = "user:123";
    std::string clustering_key = "2000-01-06";

    /* add a bunch of records */
    memtable.put(string_to_bytes(partition_key),
                 string_to_bytes(clustering_key), "age", string_to_bytes("26"));
    memtable.put(string_to_bytes(partition_key),
                 string_to_bytes(clustering_key), "name",
                 string_to_bytes("sourav"));

    /* fetch these records */
    auto age = memtable.get(string_to_bytes(partition_key),
                            string_to_bytes(clustering_key), "age");
    ASSERT_EQ(bytes_to_string(age.value()), "26");
    auto name = memtable.get(string_to_bytes(partition_key),
                             string_to_bytes(clustering_key), "name");
    ASSERT_EQ(bytes_to_string(name.value()), "sourav");
}

TEST(MemTable, put_remove_get) {
    Memtable memtable(1024);

    std::string partition_key = "user:123";
    std::string clustering_key = "2000-01-06";

    /* add a record */
    memtable.put(string_to_bytes(partition_key),
                 string_to_bytes(clustering_key), "name",
                 string_to_bytes("sourav"));

    /* remove that record */
    memtable.remove(string_to_bytes(partition_key),
                    string_to_bytes(clustering_key), "name");

    /* fetch record */
    auto name = memtable.get(string_to_bytes(partition_key),
                             string_to_bytes(clustering_key), "name");
    ASSERT_FALSE(name.has_value());
    ASSERT_EQ(name, std::nullopt);
}

TEST(MemTable, remove_non_existant) {
    Memtable memtable(1024);

    std::string partition_key = "user:123";
    std::string clustering_key = "2000-01-06";

    /* remove a record */
    memtable.remove(string_to_bytes(partition_key),
                    string_to_bytes(clustering_key), "name");

    /* fetch record */
    auto name = memtable.get(string_to_bytes(partition_key),
                             string_to_bytes(clustering_key), "name");
    ASSERT_EQ(name, std::nullopt);
    ASSERT_GT(memtable.approximate_size(), 0);
    ASSERT_EQ(memtable.count(), 1);
}

TEST(MemTable, value_overwrites) {
    Memtable memtable(1024);

    std::string partition_key = "user:123";
    std::string clustering_key = "2000-01-06";

    /* add a bunch of records */
    memtable.put(string_to_bytes(partition_key),
                 string_to_bytes(clustering_key), "name",
                 string_to_bytes("artyom"));

    /* fetch these records */
    auto artyom = memtable.get(string_to_bytes(partition_key),
                               string_to_bytes(clustering_key), "name");
    ASSERT_EQ(bytes_to_string(artyom.value()), "artyom");

    /* overwrite these recods */
    memtable.put(string_to_bytes(partition_key),
                 string_to_bytes(clustering_key), "name",
                 string_to_bytes("anna"));

    /* fetch these records */
    auto nowanna = memtable.get(string_to_bytes(partition_key),
                                string_to_bytes(clustering_key), "name");
    ASSERT_EQ(bytes_to_string(nowanna.value()), "anna");
}

TEST(MemTable, flush_uses_count) {
    Memtable memtable(50);

    std::string partition_key = "user:123";
    std::string clustering_key = "2000-01-06";

    /* add a bunch of records */
    memtable.put(string_to_bytes(partition_key),
                 string_to_bytes(clustering_key), "name",
                 string_to_bytes("artyom"));

    /* fetch these records */
    auto artyom = memtable.get(string_to_bytes(partition_key),
                               string_to_bytes(clustering_key), "name");
    ASSERT_EQ(bytes_to_string(artyom.value()), "artyom");

    ASSERT_FALSE(memtable.should_flush());

    for (size_t i = 0; i < 5; i++) {
        memtable.put(string_to_bytes("random_" + std::to_string(i)),
                     string_to_bytes("random_clus_" + std::to_string(i)),
                     "name", string_to_bytes("artyom"));
    }

    ASSERT_TRUE(memtable.should_flush());
}
