#include "enigmadb/storage/dazzle_db/compaction/compaction_policy.h"

#include <vector>

#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"
#include "gtest/gtest.h"

using namespace enigmadb;

TEST(size_tiered_compaction_policy, picks_files_clearly_from_the_tiers) {
    dazzle::SizeTieredCompactionPolicy policy_exec{3, 3};

    dazzle::SSTableMeta f1{{1}, 1000000, 10, 5};
    dazzle::SSTableMeta f2{{2}, 10000000, 10, 10};
    dazzle::SSTableMeta f3{{3}, 1000000, 10, 15};
    dazzle::SSTableMeta f4{{4}, 10000000, 10, 20};
    dazzle::SSTableMeta f5{{5}, 1000000, 10, 25};
    dazzle::SSTableMeta f6{{6}, 10000000, 10, 30};
    dazzle::SSTableMeta f7{{7}, 1000000, 10, 35};
    dazzle::SSTableMeta f8{{8}, 10000000, 10, 40};

    std::vector<const dazzle::SSTableMeta*> live{&f1, &f2, &f3, &f4,
                                                 &f5, &f6, &f7, &f8};

    auto c_task = policy_exec.pick(live);
    ASSERT_TRUE(c_task.has_value());

    auto& task = c_task.value();

    /* table 2 is older than 3 and 5 so */
    std::vector<dazzle::SSTableId> expected{
        dazzle::SSTableId{1}, dazzle::SSTableId{3}, dazzle::SSTableId{5}};

    ASSERT_FALSE(task.can_drop_tombstone);
    ASSERT_EQ(task.inputs, expected);
}

TEST(size_tiered_compaction_policy, uniform_sizes_bucket_gt_min_width) {
    dazzle::SizeTieredCompactionPolicy policy_exec{3, 3};

    dazzle::SSTableMeta f1{{1}, 1000000, 10, 5};
    dazzle::SSTableMeta f2{{2}, 1000000, 10, 10};
    dazzle::SSTableMeta f3{{3}, 1000000, 10, 15};
    dazzle::SSTableMeta f4{{4}, 1000000, 10, 20};
    dazzle::SSTableMeta f5{{5}, 1000000, 10, 25};
    dazzle::SSTableMeta f6{{6}, 1000000, 10, 30};
    dazzle::SSTableMeta f7{{7}, 1000000, 10, 35};
    dazzle::SSTableMeta f8{{8}, 1000000, 10, 40};

    std::vector<const dazzle::SSTableMeta*> live{&f1, &f2, &f3, &f4,
                                                 &f5, &f6, &f7, &f8};

    auto c_task = policy_exec.pick(live);
    ASSERT_TRUE(c_task.has_value());

    auto& task = c_task.value();

    std::vector<dazzle::SSTableId> expected{
        dazzle::SSTableId{1}, dazzle::SSTableId{2}, dazzle::SSTableId{3}};

    ASSERT_TRUE(task.can_drop_tombstone);
    ASSERT_EQ(task.inputs, expected);
}

TEST(size_tiered_compaction_policy, one_huge_with_several_small_ignores_huge) {
    dazzle::SizeTieredCompactionPolicy policy_exec{3, 4};

    dazzle::SSTableMeta f1{{1}, 1000000, 10, 5};
    dazzle::SSTableMeta f2{{2}, 1000000, 10, 10};
    dazzle::SSTableMeta f3{{3}, 1000000, 10, 15};
    dazzle::SSTableMeta f4{{4}, 90000000000, 10, 20};

    std::vector<const dazzle::SSTableMeta*> live{
        &f1,
        &f2,
        &f3,
        &f4,
    };

    auto c_task = policy_exec.pick(live);
    ASSERT_TRUE(c_task.has_value());

    auto& task = c_task.value();

    std::vector<dazzle::SSTableId> expected{
        dazzle::SSTableId{1}, dazzle::SSTableId{2}, dazzle::SSTableId{3}};

    ASSERT_TRUE(task.can_drop_tombstone);
    ASSERT_EQ(task.inputs, expected);
}

TEST(size_tiered_compaction_policy, exact_min_width) {
    dazzle::SizeTieredCompactionPolicy policy_exec{3, 3};

    dazzle::SSTableMeta f1{{1}, 1000000, 10, 5};
    dazzle::SSTableMeta f2{{2}, 1000000, 10, 10};
    dazzle::SSTableMeta f3{{3}, 1000000, 10, 15};

    std::vector<const dazzle::SSTableMeta*> live{
        &f1,
        &f2,
        &f3,
    };

    auto c_task = policy_exec.pick(live);
    ASSERT_TRUE(c_task.has_value());

    auto& task = c_task.value();

    std::vector<dazzle::SSTableId> expected{
        dazzle::SSTableId{1}, dazzle::SSTableId{2}, dazzle::SSTableId{3}};

    ASSERT_TRUE(task.can_drop_tombstone);
    ASSERT_EQ(task.inputs, expected);
}

TEST(size_tiered_compaction_policy, exact_min_width_with_less_inputs) {
    dazzle::SizeTieredCompactionPolicy policy_exec{3, 3};

    dazzle::SSTableMeta f1{{1}, 1000000, 10, 5};
    dazzle::SSTableMeta f2{{2}, 1000000, 10, 10};

    std::vector<const dazzle::SSTableMeta*> live{&f1, &f2};

    auto c_task = policy_exec.pick(live);
    ASSERT_FALSE(c_task.has_value());
}

TEST(size_tiered_compaction_policy, picks_exactly_max) {
    dazzle::SizeTieredCompactionPolicy policy_exec{3, 5};

    dazzle::SSTableMeta f1{{1}, 1000000, 10, 5};
    dazzle::SSTableMeta f2{{2}, 1000000, 10, 10};
    dazzle::SSTableMeta f3{{3}, 1000000, 10, 15};
    dazzle::SSTableMeta f4{{4}, 1000000, 10, 20};
    dazzle::SSTableMeta f5{{5}, 1000000, 10, 25};
    dazzle::SSTableMeta f6{{6}, 1000000, 10, 30};
    dazzle::SSTableMeta f7{{7}, 1000000, 10, 35};
    dazzle::SSTableMeta f8{{8}, 1000000, 10, 40};

    std::vector<const dazzle::SSTableMeta*> live{&f1, &f2, &f3, &f4,
                                                 &f5, &f6, &f7, &f8};

    auto c_task = policy_exec.pick(live);
    ASSERT_TRUE(c_task.has_value());

    auto& task = c_task.value();

    std::vector<dazzle::SSTableId> expected{
        dazzle::SSTableId{1}, dazzle::SSTableId{2}, dazzle::SSTableId{3},
        dazzle::SSTableId{4}, dazzle::SSTableId{5}};

    ASSERT_TRUE(task.can_drop_tombstone);
    ASSERT_EQ(task.inputs, expected);
}

TEST(size_tiered_compaction_policy, continuity) {
    dazzle::SizeTieredCompactionPolicy policy_exec{3, 8};

    dazzle::SSTableMeta f1{{1}, 1000000, 10, 5};
    dazzle::SSTableMeta f2{{2}, 1000000, 10, 10};
    dazzle::SSTableMeta f3{{3}, 1000000, 10, 15};
    dazzle::SSTableMeta f4{{4}, 9000000000, 10, 20};
    dazzle::SSTableMeta f5{{5}, 1000000, 10, 25};
    dazzle::SSTableMeta f6{{6}, 1000000, 10, 30};
    dazzle::SSTableMeta f7{{7}, 1000000, 10, 35};
    dazzle::SSTableMeta f8{{8}, 1000000, 10, 40};

    std::vector<const dazzle::SSTableMeta*> live{&f1, &f2, &f3, &f4,
                                                 &f5, &f6, &f7, &f8};

    auto c_task = policy_exec.pick(live);
    ASSERT_TRUE(c_task.has_value());

    auto& task = c_task.value();

    std::vector<dazzle::SSTableId> expected{
        dazzle::SSTableId{1}, dazzle::SSTableId{2}, dazzle::SSTableId{3},
        dazzle::SSTableId{5}, dazzle::SSTableId{6}, dazzle::SSTableId{7},
        dazzle::SSTableId{8}};

    ASSERT_FALSE(task.can_drop_tombstone);
    ASSERT_EQ(task.inputs, expected);

    dazzle::SSTableMeta f42{{4}, 1000000, 10, 20};
    std::vector<const dazzle::SSTableMeta*> live2{&f1, &f2, &f3, &f42,
                                                  &f5, &f6, &f7, &f8};

    auto c_task2 = policy_exec.pick(live2);
    ASSERT_TRUE(c_task2.has_value());

    auto& task2 = c_task2.value();

    std::vector<dazzle::SSTableId> expected2{
        dazzle::SSTableId{1}, dazzle::SSTableId{2}, dazzle::SSTableId{3},
        dazzle::SSTableId{4}, dazzle::SSTableId{5}, dazzle::SSTableId{6},
        dazzle::SSTableId{7}, dazzle::SSTableId{8}};

    ASSERT_TRUE(task2.can_drop_tombstone);
    ASSERT_EQ(task2.inputs, expected2);
}

TEST(size_tiered_compaction_policy, empty_live) {
    dazzle::SizeTieredCompactionPolicy policy_exec{3, 5};

    std::vector<const dazzle::SSTableMeta*> live{};

    auto c_task = policy_exec.pick(live);
    ASSERT_FALSE(c_task.has_value());
}
