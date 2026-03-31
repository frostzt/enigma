#include "enigmadb/storage/wal/wal_record.h"

#include "enigmadb/common/crc32.h"
#include "enigmadb/common/encoding.h"

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
        total_size += col.column_name.size() + 2;
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

    /* --- write body --- */
    /* Fixed Part */
    offset = encode_uint8(static_cast<uint8_t>(record.op_type), buf, offset);
    offset = encode_uint64(record.timestamp, buf, offset);
    offset = encode_uint64(record.sequence, buf, offset);

    /* Partition key */
    auto partition_key_length = record.partition_key.size();
    offset = encode_uint32(partition_key_length, buf, offset);
    memcpy(buf + offset, record.partition_key.data(), partition_key_length);
    offset += partition_key_length;

    /* Clustering key */
    auto clustering_key_length = record.clustering_key.size();
    offset = encode_uint32(clustering_key_length, buf, offset);
    memcpy(buf + offset, record.clustering_key.data(), clustering_key_length);
    offset += clustering_key_length;

    /* Columns */
    offset = encode_uint16(record.columns.size(), buf, offset);
    for (const auto& column : record.columns) {
        /* Column Name */
        auto column_name_len = column.column_name.size();
        offset = encode_uint16(column_name_len, buf, offset);
        memcpy(buf + offset, column.column_name.data(), column_name_len);
        offset += column_name_len;

        /* Column Value */
        auto column_value_len = column.value.size();
        offset = encode_uint32(column_value_len, buf, offset);
        memcpy(buf + offset, column.value.data(), column_value_len);
        offset += column_value_len;
    }

    /* update header */
    auto body_length = offset - 8;
    auto checksum = compute_crc_32(buf + 8, body_length);
    encode_uint32(body_length, buf, 0);
    encode_uint32(checksum, buf, 4);

    return out;
}

};  // namespace enigmadb::storage::wal
