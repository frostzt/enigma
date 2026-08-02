#include "enigmadb/storage/dazzle_db/compaction/tombstone_gc.h"

#include <gtest/gtest.h>

#include <vector>

#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"

using namespace enigmadb;

TEST(can_drop_tombstones, full_input_set) {
    std::vector<dazzle::SSTableId> live{{1}, {2}, {3}, {4}};
    std::vector<dazzle::SSTableId> inputs{{1}, {2}, {3}, {4}};

    ASSERT_TRUE(dazzle::can_drop_tombstones(live, inputs));

    std::vector<dazzle::SSTableId> live2{{1}};
    std::vector<dazzle::SSTableId> inputs2{{1}};

    ASSERT_TRUE(dazzle::can_drop_tombstones(live2, inputs2));
}

TEST(can_drop_tombstones, contiguous_run_from_oldest) {
    std::vector<dazzle::SSTableId> live{{1}, {2}, {3}, {4}};
    std::vector<dazzle::SSTableId> inputs{{1}, {2}};

    ASSERT_TRUE(dazzle::can_drop_tombstones(live, inputs));
}

TEST(can_drop_tombstones, gap_in_middle) {
    std::vector<dazzle::SSTableId> live{{1}, {2}, {3}, {4}};
    std::vector<dazzle::SSTableId> inputs{{2}, {4}};

    ASSERT_FALSE(dazzle::can_drop_tombstones(live, inputs));

    std::vector<dazzle::SSTableId> live2{{1}, {2}};
    std::vector<dazzle::SSTableId> inputs2{{2}};

    ASSERT_FALSE(dazzle::can_drop_tombstones(live2, inputs2));
}

TEST(can_drop_tombstones, newest_only_subset) {
    std::vector<dazzle::SSTableId> live{{1}, {2}, {3}, {4}};
    std::vector<dazzle::SSTableId> inputs{{3}, {4}};

    ASSERT_FALSE(dazzle::can_drop_tombstones(live, inputs));
}

TEST(can_drop_tombstones, empty_inputs) {
    std::vector<dazzle::SSTableId> live{{1}, {2}, {3}, {4}};
    std::vector<dazzle::SSTableId> inputs{};

    ASSERT_FALSE(dazzle::can_drop_tombstones(live, inputs));
}

TEST(can_drop_tombstones, invalid_inputs) {
    std::vector<dazzle::SSTableId> live{{1}, {2}};
    std::vector<dazzle::SSTableId> inputs{{1}, {2}, {3}, {4}};

    ASSERT_FALSE(dazzle::can_drop_tombstones(live, inputs));
}
