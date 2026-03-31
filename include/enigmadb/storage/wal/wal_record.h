/**
 * @file wal_record.h
 * @brief WAL Record representation in EnigmaDB
 *
 * @author frostzt
 * @date 2026-03-31
 */

#ifndef ENIGMA_DB_WAL_RECORD_H
#define ENIGMA_DB_WAL_RECORD_H

#include <cstdint>
#include <string>
#include <vector>

#include "enigmadb/common/error.h"
#include "enigmadb/common/result.h"

namespace enigmadb::storage::wal {

enum class WalOpType : uint8_t {
    PUT_ROW = 0x01,
    DELETE_ROW = 0x02,
    DELETE_COLUMN = 0x03,
    DELETE_PARTITION = 0x04,
};

struct WalColumn {
    std::string column_name;
    std::string value;
};

struct WalRecord {
    WalOpType op_type;
    uint64_t timestamp;
    uint64_t sequence;
    std::string partition_key;
    std::string clustering_key;
    std::vector<WalColumn> columns;
};

std::vector<char> serialize_wal_record(const WalRecord& record);

ExpectResult<WalRecord, Error> deserialize_wal_record(const char* buffer,
                                                      size_t length);

}  // namespace enigmadb::storage::wal

#endif  // ENIGMA_DB_WAL_RECORD_H
