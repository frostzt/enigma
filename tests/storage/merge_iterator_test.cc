#include "enigmadb/storage/merge_iterator.h"

#include <string>
#include <utility>
#include <vector>

#include "enigmadb/common/utils.h"
#include "enigmadb/storage/fake_iterator.h"
#include "enigmadb/storage/key_encoding.h"
#include "gtest/gtest.h"

using namespace enigmadb::common;
using namespace enigmadb::storage;
using namespace enigmadb::storage::memtable;

auto make_entry(std::string name, size_t sequence, bool is_tombstone = false) {
    return std::make_pair(
        encode_composite_key(string_to_bytes(name), string_to_bytes(name),
                             name),
        MemtableValue{string_to_bytes(name), is_tombstone, sequence});
}

TEST(merge_iterator, iterator_compare) {
    std::vector<std::string> a_to_f = {"ada",    "basic",  "cobol",
                                       "delphi", "elixir", "fortran"};

    auto ada = make_entry("ada", 1);
    auto basic = make_entry("basic", 2);
    auto cobol = make_entry("cobol", 3);
    auto delphi = make_entry("delphi", 4);
    auto elixir = make_entry("elixir", 5);
    auto fortran = make_entry("fortran", 6);

    FakeIterator itr_a({ada, delphi});
    FakeIterator itr_b({basic, elixir});
    FakeIterator itr_c({cobol, fortran});

    MergeIterator merge_itr({&itr_a, &itr_b, &itr_c});

    size_t counter = 0;
    for (merge_itr.seek_to_first(); merge_itr.valid(); merge_itr.next()) {
        auto key = merge_itr.key();
        auto value = merge_itr.value();

        auto common = string_to_bytes(a_to_f[counter]);
        auto expected_key =
            encode_composite_key(common, common, a_to_f[counter]);

        ASSERT_EQ(key, expected_key);
        ASSERT_EQ(bytes_to_string(value.data), a_to_f[counter]);
        ASSERT_EQ(value.is_tombstone, false);
        ASSERT_EQ(value.sequence, counter + 1);

        counter++;
    }

    ASSERT_EQ(counter, 6);
}

TEST(merge_iterator, uneven_lengths) {
    std::vector<std::string> a_to_f = {"ada",    "basic",  "cobol",
                                       "delphi", "elixir", "fortran"};

    auto ada = make_entry("ada", 1);
    auto basic = make_entry("basic", 2);
    auto cobol = make_entry("cobol", 3);
    auto delphi = make_entry("delphi", 4);
    auto elixir = make_entry("elixir", 5);
    auto fortran = make_entry("fortran", 6);

    FakeIterator itr_a({ada});
    FakeIterator itr_b({basic, delphi, elixir});
    FakeIterator itr_c({cobol});
    FakeIterator itr_d({fortran});
    FakeIterator itr_e({});

    MergeIterator merge_itr({&itr_a, &itr_b, &itr_c, &itr_d, &itr_e});

    size_t counter = 0;
    for (merge_itr.seek_to_first(); merge_itr.valid(); merge_itr.next()) {
        auto key = merge_itr.key();
        auto value = merge_itr.value();

        auto common = string_to_bytes(a_to_f[counter]);
        auto expected_key =
            encode_composite_key(common, common, a_to_f[counter]);

        ASSERT_EQ(key, expected_key);
        ASSERT_EQ(bytes_to_string(value.data), a_to_f[counter]);
        ASSERT_EQ(value.is_tombstone, false);
        ASSERT_EQ(value.sequence, counter + 1);

        counter++;
    }

    ASSERT_EQ(counter, 6);
}

TEST(merge_iterator, deduplication) {
    std::vector<std::string> a_to_f = {"ada",    "basic",  "cobol",
                                       "delphi", "elixir", "fortran"};

    auto ada = make_entry("ada", 1);
    auto basic = make_entry("basic", 2);
    auto cobol = make_entry("cobol", 3);
    auto delphi = make_entry("delphi", 4);
    auto elixir_zero = make_entry("elixir", 5);
    auto elixir_one = make_entry("elixir", 6);
    auto elixir_second = make_entry("elixir", 7);
    auto fortran = make_entry("fortran", 8);
    auto elixir_third = make_entry("elixir", 9);

    FakeIterator itr_a({ada, elixir_zero});
    FakeIterator itr_b({basic, delphi, elixir_one});
    FakeIterator itr_c({cobol});
    FakeIterator itr_d({elixir_second, fortran});
    FakeIterator itr_e({elixir_third});

    MergeIterator merge_itr({&itr_a, &itr_b, &itr_c, &itr_d, &itr_e});

    std::vector<size_t> expected_sequence = {1, 2, 3, 4, 9, 8};

    size_t counter = 0;
    for (merge_itr.seek_to_first(); merge_itr.valid(); merge_itr.next()) {
        auto key = merge_itr.key();
        auto value = merge_itr.value();

        auto common = string_to_bytes(a_to_f[counter]);
        auto expected_key =
            encode_composite_key(common, common, a_to_f[counter]);

        ASSERT_EQ(key, expected_key);
        ASSERT_EQ(bytes_to_string(value.data), a_to_f[counter]);
        ASSERT_EQ(value.is_tombstone, false);
        ASSERT_EQ(value.sequence, expected_sequence[counter]);

        counter++;
    }

    ASSERT_EQ(counter, 6);
}

TEST(merge_iterator, tombstone) {
    std::vector<std::string> a_to_f = {"ada",    "basic",  "cobol",
                                       "delphi", "elixir", "fortran"};

    auto ada = make_entry("ada", 1);
    auto basic = make_entry("basic", 2);
    auto cobol = make_entry("cobol", 3);
    auto delphi = make_entry("delphi", 4);
    auto elixir = make_entry("elixir", 5);
    auto fortran = make_entry("fortran", 6);
    auto delphi_is_gone = make_entry("delphi", 7, true);

    FakeIterator itr_a({ada});
    FakeIterator itr_b({basic, delphi, elixir});
    FakeIterator itr_c({cobol});
    FakeIterator itr_d({fortran});
    FakeIterator itr_e({delphi_is_gone});

    MergeIterator merge_itr({&itr_a, &itr_b, &itr_c, &itr_d, &itr_e});

    std::vector<size_t> expected_sequence = {1, 2, 3, 7, 5, 6};

    size_t counter = 0;
    for (merge_itr.seek_to_first(); merge_itr.valid(); merge_itr.next()) {
        auto key = merge_itr.key();
        auto value = merge_itr.value();

        auto common = string_to_bytes(a_to_f[counter]);
        auto expected_key =
            encode_composite_key(common, common, a_to_f[counter]);

        ASSERT_EQ(key, expected_key);
        ASSERT_EQ(bytes_to_string(value.data), a_to_f[counter]);
        if (bytes_to_string(value.data) == "delphi") {
            ASSERT_EQ(value.is_tombstone, true);
        } else {
            ASSERT_EQ(value.is_tombstone, false);
        }
        ASSERT_EQ(value.sequence, expected_sequence[counter]);

        counter++;
    }

    ASSERT_EQ(counter, 6);
}

TEST(merge_iterator, insert_beats_tombstone) {
    std::vector<std::string> a_to_f = {"ada",    "basic",  "cobol",
                                       "delphi", "elixir", "fortran"};

    auto ada = make_entry("ada", 1);
    auto basic = make_entry("basic", 2);
    auto cobol = make_entry("cobol", 3);
    auto delphi = make_entry("delphi", 4);
    auto elixir = make_entry("elixir", 5);
    auto fortran = make_entry("fortran", 6);
    auto delphi_is_gone = make_entry("delphi", 7, true);
    auto delphi_is_back = make_entry("delphi", 8);

    FakeIterator itr_a({ada});
    FakeIterator itr_b({basic, delphi, elixir});
    FakeIterator itr_c({cobol, delphi_is_gone});
    FakeIterator itr_d({fortran});
    FakeIterator itr_e({delphi_is_back});

    MergeIterator merge_itr({&itr_a, &itr_b, &itr_c, &itr_d, &itr_e});

    std::vector<size_t> expected_sequence = {1, 2, 3, 8, 5, 6};

    size_t counter = 0;
    for (merge_itr.seek_to_first(); merge_itr.valid(); merge_itr.next()) {
        auto key = merge_itr.key();
        auto value = merge_itr.value();

        auto common = string_to_bytes(a_to_f[counter]);
        auto expected_key =
            encode_composite_key(common, common, a_to_f[counter]);

        ASSERT_EQ(key, expected_key);
        ASSERT_EQ(bytes_to_string(value.data), a_to_f[counter]);
        ASSERT_EQ(value.is_tombstone, false);
        ASSERT_EQ(value.sequence, expected_sequence[counter]);

        counter++;
    }

    ASSERT_EQ(counter, 6);
}
