#include <sys/stat.h>
#include <unistd.h>

#include <vector>

#include "enigmadb/io/posix_io_engine.h"
#include "enigmadb/storage/dazzle_db/wal/wal_reader.h"
#include "enigmadb/storage/dazzle_db/wal/wal_writer.h"
#include "enigmadb/tempfile.h"
#include "enigmadb/utils.h"
#include "gtest/gtest.h"
#include "test_support/keys.h"

using namespace enigmadb;
using namespace enigmadb::TESTNAMESPACE;

dazzle::WalRecord get_test_record(
    dazzle::WalOpType type = dazzle::WalOpType::PUT_ROW) {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ts =
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    return dazzle::WalRecord{
        type,
        static_cast<uint64_t>(ts),
        1,
        make_key("user", "123", "name"),
        string_to_bytes("2026_2_APR"),
    };
}

TEST(WAL, full_flow_serialized) {
    Tempfile testfile("wal-full-flow-test-XXXXXX");
    io::PosixIOEngine engine;

    /* create writer */
    auto create_writer_result =
        dazzle::WalWriter::create(engine, testfile.path);
    ASSERT_TRUE(create_writer_result.has_value());
    auto& writer = create_writer_result.value();

    /* create records and flush */
    std::vector<dazzle::WalRecord> records(5);
    for (size_t i = 0; i < 5; i++) {
        auto record = get_test_record();
        records[i] = record;
        auto res = writer.append(record);
        ASSERT_TRUE(res.has_value());
    }
    auto sync_res = writer.sync();
    ASSERT_TRUE(sync_res.has_value());

    /* create reader */
    auto create_reader_result =
        dazzle::WalReader::create(engine, testfile.path);
    ASSERT_TRUE(create_reader_result.has_value());
    auto& reader = create_reader_result.value();

    for (size_t i = 0; i < 5; i++) {
        auto res = reader.next();
        ASSERT_TRUE(res.has_value());

        auto expected_record = records[i];
        auto record_found = res.value();

        ASSERT_EQ(record_found.key, expected_record.key);
        ASSERT_EQ(record_found.value, expected_record.value);
        ASSERT_EQ(record_found.op_type, expected_record.op_type);
        ASSERT_EQ(record_found.sequence, expected_record.sequence);
        ASSERT_EQ(record_found.timestamp, expected_record.timestamp);
    }

    /* read again to trigger eof */
    auto read_res = reader.next();
    ASSERT_FALSE(read_res.has_value());

    auto& err = read_res.error();
    ASSERT_EQ(err.code, ErrorCode::ERR_EOF);
}

TEST(WAL, crash_recovery) {
    Tempfile testfile("wal-full-flow-test-XXXXXX");
    io::PosixIOEngine engine;

    /* create writer */
    auto create_writer_result =
        dazzle::WalWriter::create(engine, testfile.path);
    ASSERT_TRUE(create_writer_result.has_value());
    auto& writer = create_writer_result.value();

    /* create records and flush */
    std::vector<dazzle::WalRecord> records(3);
    for (size_t i = 0; i < 3; i++) {
        auto record = get_test_record();
        records[i] = record;
        auto res = writer.append(record);
        ASSERT_TRUE(res.has_value());
    }
    auto sync_res = writer.sync();
    ASSERT_TRUE(sync_res.has_value());

    auto record_trunc = get_test_record();
    auto res_trunc = writer.append(record_trunc);
    ASSERT_TRUE(res_trunc.has_value());

    /* force data into the file */
    auto sync_res2 = writer.sync();
    ASSERT_TRUE(sync_res2.has_value());

    /* truncate last 5 bytes, enough to corrupt the 4th record's body
     but leave its header readable, triggering a CRC mismatch */
    {
        struct stat st;
        ASSERT_EQ(stat(testfile.path.c_str(), &st), 0);
        ASSERT_EQ(truncate(testfile.path.c_str(), st.st_size - 5), 0);
    }

    /* create reader */
    auto create_reader_result =
        dazzle::WalReader::create(engine, testfile.path);
    ASSERT_TRUE(create_reader_result.has_value());
    auto& reader = create_reader_result.value();

    for (size_t i = 0; i < 3; i++) {
        auto res = reader.next();
        ASSERT_TRUE(res.has_value());

        auto expected_record = records[i];
        auto record_found = res.value();

        ASSERT_EQ(record_found.key, expected_record.key);
        ASSERT_EQ(record_found.value, expected_record.value);
        ASSERT_EQ(record_found.op_type, expected_record.op_type);
        ASSERT_EQ(record_found.sequence, expected_record.sequence);
        ASSERT_EQ(record_found.timestamp, expected_record.timestamp);
    }

    /* 4th record should be corrupted */
    auto read_res = reader.next();
    ASSERT_FALSE(read_res.has_value());

    auto& err = read_res.error();
    ASSERT_EQ(err.code, ErrorCode::BAD_CONFIG);
}
