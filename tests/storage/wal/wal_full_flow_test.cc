#include <vector>

#include "enigmadb/common/tempfile.h"
#include "enigmadb/common/utils.h"
#include "enigmadb/io/posix_io_engine.hpp"
#include "enigmadb/storage/wal/wal_reader.h"
#include "enigmadb/storage/wal/wal_writer.h"
#include "gtest/gtest.h"

using namespace enigmadb::common;
using namespace enigmadb::storage::wal;

WalRecord get_test_record(size_t column_count,
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

TEST(WAL, full_flow_serialized) {
    Tempfile testfile("wal-full-flow-test-XXXXXX");
    enigmadb::io::PosixIOEngine engine;

    /* create writer */
    auto create_writer_result = WalWriter::create(engine, testfile.path);
    ASSERT_TRUE(create_writer_result.has_value());
    auto& writer = create_writer_result.value();

    /* create records and flush */
    std::vector<WalRecord> records(5);
    for (size_t i = 0; i < 5; i++) {
        auto record = get_test_record(i);
        records[i] = record;
        auto res = writer.append(record);
        ASSERT_TRUE(res.has_value());
    }
    auto sync_res = writer.sync();
    ASSERT_TRUE(sync_res.has_value());

    /* create reader */
    auto create_reader_result = WalReader::create(engine, testfile.path);
    ASSERT_TRUE(create_reader_result.has_value());
    auto& reader = create_reader_result.value();

    for (size_t i = 0; i < 5; i++) {
        auto res = reader.next();
        ASSERT_TRUE(res.has_value());

        auto expected_record = records[i];
        auto record_found = res.value();

        ASSERT_EQ(record_found.clustering_key, expected_record.clustering_key);
        ASSERT_EQ(record_found.partition_key, expected_record.partition_key);
        ASSERT_EQ(record_found.op_type, expected_record.op_type);
        ASSERT_EQ(record_found.sequence, expected_record.sequence);
        ASSERT_EQ(record_found.timestamp, expected_record.timestamp);

        ASSERT_EQ(record_found.columns.size(), expected_record.columns.size());
        for (size_t j = 0; j < expected_record.columns.size(); j++) {
            ASSERT_EQ(record_found.columns[j].name,
                      expected_record.columns[j].name);
            ASSERT_EQ(record_found.columns[j].value,
                      expected_record.columns[j].value);
        }
    }

    /* read again to trigger eof */
    auto read_res = reader.next();
    ASSERT_FALSE(read_res.has_value());

    auto& err = read_res.err();
    ASSERT_EQ(err.code, ErrorCode::ERR_EOF);
}
