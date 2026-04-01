#include "enigmadb/storage/wal/wal_record.h"

#include "enigmadb/common/crc32.h"
#include "enigmadb/common/encoding.h"
#include "enigmadb/common/error.h"
#include "enigmadb/common/result.h"

namespace enigmadb::storage::wal {

size_t get_record_size(const WalRecord& record) {
    size_t total_size = /* Length */ 4 + /* CRC */ 4 +
                        /* WalOpType */ 1 + /* timestamp */ 8 +
                        /* sequence */ 8;
    total_size += record.partition_key.size() + 4;
    total_size += record.clustering_key.size() + 4;
    /* Columns */
    total_size += 2; /* Column Count size */
    for (const auto& col : record.columns) {
        total_size += col.name.size() + 2;
        total_size += col.value.size() + 4;
    }
    return total_size;
}

std::vector<uint8_t> serialize_wal_record(const WalRecord& record) {
    auto to_alloc = get_record_size(record);

    std::vector<uint8_t> out(to_alloc);
    auto buf = out.data();

    size_t offset = 0;

    /* --- write header (pre-alloc) --- */
    offset = encode_uint32(0, buf, offset);
    offset = encode_uint32(0, buf, offset);

    /* --- write fixed body --- */
    offset = encode_uint8(static_cast<uint8_t>(record.op_type), buf, offset);
    offset = encode_uint64(record.timestamp, buf, offset);
    offset = encode_uint64(record.sequence, buf, offset);

    /* --- write partition key --- */
    auto partition_key_length = record.partition_key.size();
    offset = encode_uint32(partition_key_length, buf, offset);
    memcpy(buf + offset, record.partition_key.data(), partition_key_length);
    offset += partition_key_length;

    /* --- write clustering key --- */
    auto clustering_key_length = record.clustering_key.size();
    offset = encode_uint32(clustering_key_length, buf, offset);
    memcpy(buf + offset, record.clustering_key.data(), clustering_key_length);
    offset += clustering_key_length;

    /* --- write columns --- */
    offset = encode_uint16(static_cast<uint16_t>(record.columns.size()), buf,
                           offset);
    for (const auto& column : record.columns) {
        /* Column Name */
        auto cname_len = column.name.size();
        offset = encode_uint16(cname_len, buf, offset);
        offset = encode_bytes(column.name.data(), cname_len, buf, offset);

        /* Column Value */
        auto cvalue_len = column.value.size();
        offset = encode_uint32(cvalue_len, buf, offset);
        offset = encode_bytes(column.value.data(), cvalue_len, buf, offset);
    }

    /* update header */
    auto body_length = offset - 8;
    auto checksum = compute_crc_32(buf + 8, body_length);
    encode_uint32(body_length, buf, 0);
    encode_uint32(checksum, buf, 4);

    return out;
}

ExpectResult<WalRecord, Error> deserialize_wal_record(const uint8_t* buffer,
                                                      size_t length) {
    WalRecord record;
    size_t offset = 0;

    constexpr size_t MIN_RECORD_SIZE = 35;
    if (length < MIN_RECORD_SIZE) {
        return ExpectResult<WalRecord, Error>::err(Error{
            ErrorCode::READ_OUT_OF_RANGE, "buffer too small for WAL record"});
    }

    /* --- read header --- */
    auto body_length = decode_uint32(buffer, offset);
    offset += 4;
    auto checksum = decode_uint32(buffer, offset);
    offset += 4;

    /* validate checksum */
    auto gen_checksum = compute_crc_32(buffer + 8, body_length);
    if (checksum != gen_checksum) {
        return ExpectResult<WalRecord, Error>::err(Error{
            ErrorCode::BAD_CONFIG, "checksum mismatch corrupted data found"});
    }

    /* --- read body fixed parts --- */
    record.op_type = static_cast<WalOpType>(decode_uint8(buffer, offset));
    offset += 1;
    record.timestamp = decode_uint64(buffer, offset);
    offset += 8;
    record.sequence = decode_uint64(buffer, offset);
    offset += 8;

    /* --- partition key --- */
    auto partition_key_len = decode_uint32(buffer, offset);
    offset += 4;
    if (offset + partition_key_len > length) {
        return ExpectResult<WalRecord, Error>::err(Error{
            ErrorCode::READ_OUT_OF_RANGE, "partitioning key out of range"});
    }
    record.partition_key.assign(buffer + offset,
                                buffer + offset + partition_key_len);
    offset += partition_key_len;

    /* --- clustering key --- */
    auto clustering_key_len = decode_uint32(buffer, offset);
    offset += 4;
    if (offset + clustering_key_len > length) {
        return ExpectResult<WalRecord, Error>::err(
            Error{ErrorCode::READ_OUT_OF_RANGE, "clustering key out of range"});
    }
    record.clustering_key.assign(buffer + offset,
                                 buffer + offset + clustering_key_len);
    offset += clustering_key_len;

    /* --- columns --- */
    auto columns = decode_uint16(buffer, offset);
    offset += 2;
    for (size_t i = 0; i < columns; i++) {
        WalColumn column;

        /* --- column name --- */
        auto cname_len = decode_uint16(buffer, offset);
        offset += 2;
        if (offset + cname_len > length) {
            return ExpectResult<WalRecord, Error>::err(Error{
                ErrorCode::READ_OUT_OF_RANGE, "column name out of range"});
        }
        column.name.assign(reinterpret_cast<const char*>(buffer + offset),
                           cname_len);
        offset += cname_len;

        /* --- column value --- */
        auto cvalue_len = decode_uint32(buffer, offset);
        offset += 4;
        if (offset + cvalue_len > length) {
            return ExpectResult<WalRecord, Error>::err(Error{
                ErrorCode::READ_OUT_OF_RANGE, "column value out of range"});
        }
        column.value.assign(buffer + offset, buffer + offset + cvalue_len);
        offset += cvalue_len;

        record.columns.push_back(column);
    }

    return ExpectResult<WalRecord, Error>::ok(std::move(record));
}

};  // namespace enigmadb::storage::wal
