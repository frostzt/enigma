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

template <typename T>
using WalResult = common::ExpectResult<T, common::Error>;

enum class WalOpType : uint8_t {
    PUT_ROW = 0x01,
    DELETE_ROW = 0x02,
    DELETE_COLUMN = 0x03,
    DELETE_PARTITION = 0x04,
};

struct WalColumn {
    std::string name;
    std::vector<uint8_t> value;
};

struct WalRecord {
    WalOpType op_type;
    uint64_t timestamp;
    uint64_t sequence;
    std::vector<uint8_t> partition_key;
    std::vector<uint8_t> clustering_key;
    std::vector<WalColumn> columns;
};

size_t get_record_size(const WalRecord& record);

// clang-format off
/**
 * @brief Serializes the WAL Record into raw bytes
 *
 * Here is how the serialization is done and how it looks like
 *
 * 0  1  2  3  4  5  6  7  0  1  2  3  4  5  6  7  - bytes NOT bits
 * |   LENGTH  |   CRC32   |OP| Timestamp -> next byte too
 *    |       Sequence        | P.KEY LEN |  PART KEY ARBITRARY |
 * | C.KEY LEN | CLUS KEY ARBITRARY |C.LEN|CNAME|
 * | COL NAME ARBITRARY | COL.VALUE | COL VALUE ARBITRARY
 *
 * --- HEADER ---
 * Length (4 bytes) -- Length of the entire record
 * CRC32  (4 bytes) -- CRC Checksum of the entire record
 *
 * --- BODY ---
 * Wal Op Type           (1 byte)  -- Type of operation
 * Timestamp             (8 bytes) -- Timestamp of this operation
 * Sequence              (8 bytes) -- Ever increasing number assigned to this op
 * Partition Key Length  (4 bytes) -- Length of the next byte sequence containing Partition key
 * Partition Key       (ARBITRARY) -- Partition key name
 * Clustering Key Length (4 bytes) -- Length of the next byte sequence containing Clustering key
 * Clustering Key      (ARBITRARY) -- Clustering key name
 * Columns               (2 bytes) -- Numbers of column
 *    --- REPEATED xColumns
 *    Column Name Size            (2 bytes) -- Length of the column name
 *    Column Name               (ARBITRARY) -- Name of the column
 *    Column Value Size           (4 bytes) -- Length of the column value
 *    Column Value              (ARBITRARY) -- Column value
 *    --- REPEATED xColumns
 */
std::vector<uint8_t> serialize_wal_record(const WalRecord& record);
// clang-format on

enigmadb::common::ExpectResult<WalRecord, enigmadb::common::Error>
deserialize_wal_record(const uint8_t* buffer, size_t length);

}  // namespace enigmadb::storage::wal

#endif  // ENIGMA_DB_WAL_RECORD_H
