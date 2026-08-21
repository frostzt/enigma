#include "enigmadb/storage/dazzle_db/core/version_edit.h"

#include <gtest/gtest.h>

#include <vector>

#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"

using namespace enigmadb;

dazzle::VersionEdit get_record(size_t r = 1, size_t a = 1, uint64_t next_sst_id = 0) {
    std::vector<dazzle::SSTableId> sstids(r);
    for (size_t i = 0; i < r; i++) {
        sstids.push_back(dazzle::SSTableId{i});
    }
    std::vector<dazzle::SSTableMeta> sstmetas(a);
    for (size_t i = 0; i < a; i++) {
        sstmetas.push_back(dazzle::SSTableMeta{dazzle::SSTableId{1}, 1000, 500, 50});
    }
    return dazzle::VersionEdit(std::move(sstids), std::move(sstmetas), next_sst_id);
}

TEST(VersionEdit, round_trip) {
    auto record = get_record(5, 5, 1);
    auto serialized_record = serialize_version_edit(record);

    auto size = dazzle::get_version_edit_record_size(record);
    ASSERT_EQ(size, serialized_record.size());

    dazzle::VersionEdit ve;
    auto deserialized_result = dazzle::deserialize_version_edit(serialized_record.data(), size, ve);

    ASSERT_TRUE(deserialized_result.has_value());
    auto deserialized_size = deserialized_result.value();

    ASSERT_EQ(deserialized_size, size);
    ASSERT_EQ(record.next_sst_id, ve.next_sst_id);

    for (size_t i = 0; i < 5; i++) {
        ASSERT_EQ(record.removed[i], ve.removed[i]);
        ASSERT_EQ(record.added[i].id, ve.added[i].id);
        ASSERT_EQ(record.added[i].size_bytes, ve.added[i].size_bytes);
        ASSERT_EQ(record.added[i].entry_count, ve.added[i].entry_count);
        ASSERT_EQ(record.added[i].max_sequence, ve.added[i].max_sequence);
    }
}

TEST(VersionEdit, detects_corruption) {
    auto record = get_record(5, 5, 1);
    auto serialized_record = serialize_version_edit(record);

    auto size = dazzle::get_version_edit_record_size(record);
    ASSERT_EQ(size, serialized_record.size());

    /* flip random bytes */
    serialized_record[52] ^= 0xFF;
    serialized_record[53] ^= 0xFF;

    dazzle::VersionEdit ve;
    auto deserialized_result = dazzle::deserialize_version_edit(serialized_record.data(), size, ve);

    ASSERT_FALSE(deserialized_result.has_value());
    auto& error = deserialized_result.error();

    ASSERT_EQ(error.code, ErrorCode::CHECKSUM_MISMATCH);
    ASSERT_TRUE(error.is_checksum_mismatch());
}

TEST(VersionEdit, truncated_buffer) {
    auto record = get_record(5, 5, 1);
    auto serialized_record = serialize_version_edit(record);

    auto size = dazzle::get_version_edit_record_size(record);
    ASSERT_EQ(size, serialized_record.size());

    std::vector<uint8_t> truncated(serialized_record.begin(), serialized_record.begin() + size / 1.25);

    dazzle::VersionEdit ve;
    auto deserialized_result = dazzle::deserialize_version_edit(truncated.data(), truncated.size(), ve);

    ASSERT_FALSE(deserialized_result.has_value());
    auto& error = deserialized_result.error();

    ASSERT_EQ(error.code, ErrorCode::INCOMPLETE_RECORD);
}
