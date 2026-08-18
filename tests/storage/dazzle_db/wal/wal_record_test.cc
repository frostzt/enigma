#include "enigmadb/storage/dazzle_db/wal/wal_record.h"

#include "enigmadb/utils.h"
#include "gtest/gtest.h"
#include "test_support/keys.h"

using namespace enigmadb;
using namespace enigmadb::TESTNAMESPACE;

dazzle::WalRecord get_record(dazzle::WalOpType type = dazzle::WalOpType::PUT_ROW) {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    return dazzle::WalRecord{
        type, static_cast<uint64_t>(ts), 1, make_key("user", "123", "name"), string_to_bytes("2026_2_APR"),
    };
}

TEST(WAL_Record, roundtrip) {
    auto record = get_record();
    auto serialized_record = serialize_wal_record(record);

    auto size = get_record_size(record);
    ASSERT_EQ(size, serialized_record.size());

    auto deserialized_result = dazzle::deserialize_wal_record(serialized_record.data(), size);

    ASSERT_TRUE(deserialized_result.has_value());
    auto deserialized_record = deserialized_result.value();

    ASSERT_EQ(record.op_type, deserialized_record.op_type);
    ASSERT_EQ(record.timestamp, deserialized_record.timestamp);
    ASSERT_EQ(record.sequence, deserialized_record.sequence);
}

TEST(WAL_Record, detects_corruption) {
    auto record = get_record();
    auto serialized_record = serialize_wal_record(record);

    auto size = get_record_size(record);
    ASSERT_EQ(size, serialized_record.size());

    /* corrupting record */
    serialized_record[32] ^= 0xFF;
    serialized_record[33] ^= 0xFF;

    auto deserialized_result = dazzle::deserialize_wal_record(serialized_record.data(), size);

    ASSERT_FALSE(deserialized_result.has_value());
    auto& error = deserialized_result.error();

    ASSERT_EQ(error.code, ErrorCode::BAD_CONFIG);
    ASSERT_EQ(error.message, "checksum mismatch corrupted data found");
}

TEST(WAL_Record, truncated_buffer) {
    auto record = get_record();
    auto serialized_record = serialize_wal_record(record);

    auto size = get_record_size(record);
    ASSERT_EQ(size, serialized_record.size());

    std::vector<uint8_t> truncated(serialized_record.begin(), serialized_record.begin() + size / 1.25);

    auto deserialized_result = dazzle::deserialize_wal_record(truncated.data(), truncated.size());

    ASSERT_FALSE(deserialized_result.has_value());
    auto& error = deserialized_result.error();

    ASSERT_EQ(error.code, ErrorCode::READ_OUT_OF_RANGE);
    ASSERT_EQ(error.message, "out of range");
}
