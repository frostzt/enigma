#include "enigmadb/storage/dazzle_db/wal/wal_record.h"

#include "enigmadb/base.h"
#include "enigmadb/crc32.h"
#include "enigmadb/encoding.h"

namespace enigmadb::dazzle {

size_t get_record_size(const WalRecord& record) {
    size_t total_size = /* Length */ 4 + /* CRC */ 4 +
                        /* WalOpType */ 1 + /* timestamp */ 8 +
                        /* sequence */ 8;
    total_size += record.key.size() + /* Key length */ 4;
    total_size += record.value.size() + /* Value length */ 4;
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

    /* --- write key --- */
    auto key_length = record.key.size();
    offset = encode_uint32(key_length, buf, offset);
    if (key_length > 0) {
        memcpy(buf + offset, record.key.bytes().data(), key_length);
        offset += key_length;
    }

    /* --- write value --- */
    auto value_length = record.value.size();
    offset = encode_uint32(value_length, buf, offset);
    if (value_length > 0) {
        memcpy(buf + offset, record.value.data(), value_length);
        offset += value_length;
    }

    /* update header */
    auto body_length = offset - 8;
    auto checksum = compute_crc_32(buf + 8, body_length);
    encode_uint32(body_length, buf, 0);
    encode_uint32(checksum, buf, 4);

    return out;
}

Result<WalRecord> deserialize_wal_record(const uint8_t* buffer, size_t length) {
    WalRecord record;
    size_t offset = 0;

    constexpr size_t MIN_RECORD_SIZE = 33;
    if (length < MIN_RECORD_SIZE) {
        return Result<WalRecord>::err(Error{ErrorCode::READ_OUT_OF_RANGE,
                                            "buffer too small for WAL record"});
    }

    /* --- read header --- */
    auto body_length = decode_uint32(buffer, offset);
    offset += 4;
    auto checksum = decode_uint32(buffer, offset);
    offset += 4;

    if (body_length > length - 8) {
        return Result<WalRecord>::err(
            Error{ErrorCode::READ_OUT_OF_RANGE, "out of range"});
    }

    /* validate checksum */
    auto gen_checksum = compute_crc_32(buffer + 8, body_length);
    if (checksum != gen_checksum) {
        return Result<WalRecord>::err(Error{
            ErrorCode::BAD_CONFIG, "checksum mismatch corrupted data found"});
    }

    /* --- read body fixed parts --- */
    record.op_type = static_cast<WalOpType>(decode_uint8(buffer, offset));
    offset += 1;
    record.timestamp = decode_uint64(buffer, offset);
    offset += 8;
    record.sequence = decode_uint64(buffer, offset);
    offset += 8;

    /* --- key --- */
    auto key_len = decode_uint32(buffer, offset);
    offset += 4;
    if (offset + key_len > length) {
        return Result<WalRecord>::err(
            Error{ErrorCode::READ_OUT_OF_RANGE, "key out of range"});
    }
    record.key.assign(buffer + offset, buffer + offset + key_len);
    offset += key_len;

    /* --- value --- */
    auto value_len = decode_uint32(buffer, offset);
    offset += 4;
    if (offset + value_len > length) {
        return Result<WalRecord>::err(
            Error{ErrorCode::READ_OUT_OF_RANGE, "value out of range"});
    }
    record.value.assign(buffer + offset, buffer + offset + value_len);
    offset += value_len;

    return Result<WalRecord>::ok(std::move(record));
}

};  // namespace enigmadb::dazzle
