#include "enigmadb/storage/wal/wal_record.h"

#include "enigmadb/common/utils.h"
#include "gtest/gtest.h"

using namespace enigmadb::common;
using namespace enigmadb::storage::wal;

WalRecord get_a_record(size_t column_count,
                       WalOpType type = WalOpType::PUT_ROW) {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ts =
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::vector<WalColumn> columns = {};
    for (size_t i = 0; i < column_count; i++) {
        columns.push_back(
            WalColumn{"col__name__" + std::to_string(i),
                      string_to_bytes("col__value__" + std::to_string(i))});
    }

    return WalRecord{
        type,
        static_cast<uint64_t>(ts),
        1,
        string_to_bytes("user:123"),
        string_to_bytes("2026_2_APR"),
        columns,
    };
}

TEST(WAL_Record, roundtrip_with_one_column) {
    auto record = get_a_record(1);
    auto serialized_record = serialize_wal_record(record);

    auto size = get_record_size(record);
    ASSERT_EQ(size, serialized_record.size());

    auto deserialized_result =
        deserialize_wal_record(serialized_record.data(), size);

    ASSERT_TRUE(deserialized_result.has_value());
    auto deserialized_record = deserialized_result.value();

    ASSERT_EQ(record.op_type, deserialized_record.op_type);
    ASSERT_EQ(record.timestamp, deserialized_record.timestamp);
    ASSERT_EQ(record.sequence, deserialized_record.sequence);
    ASSERT_EQ(record.partition_key, deserialized_record.partition_key);
    ASSERT_EQ(record.clustering_key, deserialized_record.clustering_key);
    ASSERT_EQ(record.columns.size(), deserialized_record.columns.size());

    for (size_t i = 0; i < record.columns.size(); i++) {
        ASSERT_EQ(record.columns[i].name, deserialized_record.columns[i].name);
        ASSERT_EQ(record.columns[i].value,
                  deserialized_record.columns[i].value);
    }
}

TEST(WAL_Record, roundtrip_with_multiple_column) {
    auto record = get_a_record(5);
    auto serialized_record = serialize_wal_record(record);

    auto size = get_record_size(record);
    ASSERT_EQ(size, serialized_record.size());

    auto deserialized_result =
        deserialize_wal_record(serialized_record.data(), size);

    ASSERT_TRUE(deserialized_result.has_value());
    auto deserialized_record = deserialized_result.value();

    ASSERT_EQ(record.op_type, deserialized_record.op_type);
    ASSERT_EQ(record.timestamp, deserialized_record.timestamp);
    ASSERT_EQ(record.sequence, deserialized_record.sequence);
    ASSERT_EQ(record.partition_key, deserialized_record.partition_key);
    ASSERT_EQ(record.clustering_key, deserialized_record.clustering_key);
    ASSERT_EQ(record.columns.size(), deserialized_record.columns.size());

    for (size_t i = 0; i < record.columns.size(); i++) {
        ASSERT_EQ(record.columns[i].name, deserialized_record.columns[i].name);
        ASSERT_EQ(record.columns[i].value,
                  deserialized_record.columns[i].value);
    }
}

TEST(WAL_Record, roundtrip_with_no_columns) {
    auto record = get_a_record(0, WalOpType::DELETE_PARTITION);
    auto serialized_record = serialize_wal_record(record);

    auto size = get_record_size(record);
    ASSERT_EQ(size, serialized_record.size());

    auto deserialized_result =
        deserialize_wal_record(serialized_record.data(), size);

    ASSERT_TRUE(deserialized_result.has_value());
    auto deserialized_record = deserialized_result.value();

    ASSERT_EQ(record.op_type, deserialized_record.op_type);
    ASSERT_EQ(record.timestamp, deserialized_record.timestamp);
    ASSERT_EQ(record.sequence, deserialized_record.sequence);
    ASSERT_EQ(record.partition_key, deserialized_record.partition_key);
    ASSERT_EQ(record.clustering_key, deserialized_record.clustering_key);
    ASSERT_EQ(0, record.columns.size());
    ASSERT_EQ(0, deserialized_record.columns.size());
}

TEST(WAL_Record, empty_partitioning_keys) {
    auto record = get_a_record(3);
    record.partition_key = string_to_bytes("");

    auto serialized_record = serialize_wal_record(record);

    auto size = get_record_size(record);
    ASSERT_EQ(size, serialized_record.size());

    auto deserialized_result =
        deserialize_wal_record(serialized_record.data(), size);

    ASSERT_TRUE(deserialized_result.has_value());
    auto deserialized_record = deserialized_result.value();

    ASSERT_EQ(record.op_type, deserialized_record.op_type);
    ASSERT_EQ(record.timestamp, deserialized_record.timestamp);
    ASSERT_EQ(record.sequence, deserialized_record.sequence);
    ASSERT_EQ(record.partition_key, deserialized_record.partition_key);
    ASSERT_EQ(record.clustering_key, deserialized_record.clustering_key);
    for (size_t i = 0; i < record.columns.size(); i++) {
        ASSERT_EQ(record.columns[i].name, deserialized_record.columns[i].name);
        ASSERT_EQ(record.columns[i].value,
                  deserialized_record.columns[i].value);
    }
}

TEST(WAL_Record, empty_clustering_keys) {
    auto record = get_a_record(3);
    record.clustering_key = string_to_bytes("");

    auto serialized_record = serialize_wal_record(record);

    auto size = get_record_size(record);
    ASSERT_EQ(size, serialized_record.size());

    auto deserialized_result =
        deserialize_wal_record(serialized_record.data(), size);

    ASSERT_TRUE(deserialized_result.has_value());
    auto deserialized_record = deserialized_result.value();

    ASSERT_EQ(record.op_type, deserialized_record.op_type);
    ASSERT_EQ(record.timestamp, deserialized_record.timestamp);
    ASSERT_EQ(record.sequence, deserialized_record.sequence);
    ASSERT_EQ(record.partition_key, deserialized_record.partition_key);
    ASSERT_EQ(record.clustering_key, deserialized_record.clustering_key);
    for (size_t i = 0; i < record.columns.size(); i++) {
        ASSERT_EQ(record.columns[i].name, deserialized_record.columns[i].name);
        ASSERT_EQ(record.columns[i].value,
                  deserialized_record.columns[i].value);
    }
}

TEST(WAL_Record, empty_column_values) {
    auto record = get_a_record(1);
    record.columns[0].value = string_to_bytes("");
    auto serialized_record = serialize_wal_record(record);

    auto size = get_record_size(record);
    ASSERT_EQ(size, serialized_record.size());

    auto deserialized_result =
        deserialize_wal_record(serialized_record.data(), size);

    ASSERT_TRUE(deserialized_result.has_value());
    auto deserialized_record = deserialized_result.value();

    ASSERT_EQ(record.op_type, deserialized_record.op_type);
    ASSERT_EQ(record.timestamp, deserialized_record.timestamp);
    ASSERT_EQ(record.sequence, deserialized_record.sequence);
    ASSERT_EQ(record.partition_key, deserialized_record.partition_key);
    ASSERT_EQ(record.clustering_key, deserialized_record.clustering_key);
    for (size_t i = 0; i < record.columns.size(); i++) {
        ASSERT_EQ(record.columns[i].name, deserialized_record.columns[i].name);
        ASSERT_EQ(record.columns[i].value,
                  deserialized_record.columns[i].value);
    }
}

TEST(WAL_Record, detects_corruption) {
    auto record = get_a_record(1);
    auto serialized_record = serialize_wal_record(record);

    auto size = get_record_size(record);
    ASSERT_EQ(size, serialized_record.size());

    /* corrupting record */
    serialized_record[32] ^= 0xFF;
    serialized_record[33] ^= 0xFF;

    auto deserialized_result =
        deserialize_wal_record(serialized_record.data(), size);

    ASSERT_FALSE(deserialized_result.has_value());
    auto& error = deserialized_result.err();

    ASSERT_EQ(error.code, ErrorCode::BAD_CONFIG);
    ASSERT_EQ(error.message, "checksum mismatch corrupted data found");
}

TEST(WAL_Record, truncated_buffer) {
    auto record = get_a_record(1);
    auto serialized_record = serialize_wal_record(record);

    auto size = get_record_size(record);
    ASSERT_EQ(size, serialized_record.size());

    std::vector<uint8_t> truncated(serialized_record.begin(),
                                   serialized_record.begin() + size / 2);

    auto deserialized_result =
        deserialize_wal_record(truncated.data(), truncated.size());

    ASSERT_FALSE(deserialized_result.has_value());
    auto& error = deserialized_result.err();

    ASSERT_EQ(error.code, ErrorCode::READ_OUT_OF_RANGE);
    ASSERT_EQ(error.message, "out of range");
}
