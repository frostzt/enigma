#include "enigmadb/storage/sstable/sstable_common.h"

#include "gtest/gtest.h"

using namespace enigmadb::storage::sstable;

TEST(sstable_common, sstable_filename_generates_names) {
    SSTableId sstid1{1};
    SSTableId sstid10{10};
    SSTableId sstid101{101};
    SSTableId sstid1010{1010};
    SSTableId sstid10101{10101};
    SSTableId sstid101010{101010};

    EXPECT_EQ(sstable_filename(sstid1), "sst_00000001.db");
    EXPECT_EQ(sstable_filename(sstid10), "sst_00000010.db");
    EXPECT_EQ(sstable_filename(sstid101), "sst_00000101.db");
    EXPECT_EQ(sstable_filename(sstid1010), "sst_00001010.db");
    EXPECT_EQ(sstable_filename(sstid10101), "sst_00010101.db");
    EXPECT_EQ(sstable_filename(sstid101010), "sst_00101010.db");
}

TEST(sstable_common, sstable_parse_filename) {
    EXPECT_EQ(1, parse_sstable_filename("sst_00000001.db").value);
    EXPECT_EQ(10, parse_sstable_filename("sst_00000010.db").value);
    EXPECT_EQ(101, parse_sstable_filename("sst_00000101.db").value);
    EXPECT_EQ(1010, parse_sstable_filename("sst_00001010.db").value);
    EXPECT_EQ(10101, parse_sstable_filename("sst_00010101.db").value);
    EXPECT_EQ(101010, parse_sstable_filename("sst_00101010.db").value);
}
