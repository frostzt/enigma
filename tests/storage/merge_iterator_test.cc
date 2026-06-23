#include "enigmadb/storage/merge_iterator.h"

#include <utility>
#include <vector>

#include "enigmadb/common/utils.h"
#include "enigmadb/storage/fake_iterator.h"
#include "enigmadb/storage/key_encoding.h"
#include "gtest/gtest.h"

using namespace enigmadb::common;
using namespace enigmadb::storage;
using namespace enigmadb::storage::memtable;

TEST(merge_iterator, iterator_compare) {
    std::vector<std::string> atof = {"ada",    "basic",  "cobol",
                                     "delphi", "elixir", "fortran"};

    // pairs
    auto ada =
        std::make_pair(encode_composite_key(string_to_bytes(atof[0]),
                                            string_to_bytes(atof[0]), "ada"),
                       MemtableValue{string_to_bytes(atof[0]), false, 1});
    auto basic =
        std::make_pair(encode_composite_key(string_to_bytes(atof[1]),
                                            string_to_bytes(atof[1]), "basic"),
                       MemtableValue{string_to_bytes(atof[1]), false, 2});
    auto cobol =
        std::make_pair(encode_composite_key(string_to_bytes(atof[2]),
                                            string_to_bytes(atof[2]), "cobol"),
                       MemtableValue{string_to_bytes(atof[2]), false, 3});
    auto delphi =
        std::make_pair(encode_composite_key(string_to_bytes(atof[3]),
                                            string_to_bytes(atof[3]), "delphi"),
                       MemtableValue{string_to_bytes(atof[3]), false, 4});
    auto elixir =
        std::make_pair(encode_composite_key(string_to_bytes(atof[4]),
                                            string_to_bytes(atof[4]), "elixir"),
                       MemtableValue{string_to_bytes(atof[4]), false, 5});
    auto fortran = std::make_pair(
        encode_composite_key(string_to_bytes(atof[5]), string_to_bytes(atof[5]),
                             "fortran"),
        MemtableValue{string_to_bytes(atof[5]), false, 6});

    // iters
    FakeIterator itr_a({ada, delphi});
    FakeIterator itr_b({basic, elixir});
    FakeIterator itr_c({cobol, fortran});

    MergeIterator merge_itr({&itr_a, &itr_b, &itr_c});

    size_t counter = 0;
    for (merge_itr.seek_to_first(); merge_itr.valid(); merge_itr.next()) {
        auto key = merge_itr.key();
        auto value = merge_itr.value();

        auto common = string_to_bytes(atof[counter]);
        auto expected_key = encode_composite_key(common, common, atof[counter]);

        ASSERT_EQ(key, expected_key);
        ASSERT_EQ(bytes_to_string(value.data), atof[counter]);
        ASSERT_EQ(value.is_tombstone, false);
        ASSERT_EQ(value.sequence, counter + 1);

        counter++;
    }

    ASSERT_EQ(counter, 6);
}
