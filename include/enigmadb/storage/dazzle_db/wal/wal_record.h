/**
 * @file wal_record.h
 * @brief WAL Record representation in EnigmaDB
 *
 * @author frostzt
 * @date 2026-03-31
 */

#ifndef ENIGMA_DB_DAZZLE_WAL_RECORD_H
#define ENIGMA_DB_DAZZLE_WAL_RECORD_H

#include <cstdint>
#include <vector>

#include "enigmadb/base.h"
#include "enigmadb/storage/key.h"

namespace enigmadb::dazzle {

enum class WalOpType : uint8_t {
    PUT_ROW = 0x01,
    DELETE_ROW = 0x02,
};

struct WalRecord {
    WalOpType op_type;
    uint64_t timestamp;
    uint64_t sequence;
    storage::Key key;
    std::vector<uint8_t> value;
};

size_t get_record_size(const WalRecord& record);

// clang-format off
/**
 * @brief Serializes the WAL Record into raw bytes
 *
 * --- HEADER ---
 * Length (4 bytes) -- Length of the entire record
 * CRC32  (4 bytes) -- CRC Checksum of the entire record
 *
 * --- BODY ---
 * Wal Op Type           (1 byte)      -- Type of operation
 * Timestamp             (8 bytes)     -- Timestamp of this operation
 * Sequence              (8 bytes)     -- Ever increasing number assigned to this op
 * Key Length            (4 bytes)     -- Length of the next byte sequence containing key
 * Key                   (ARBITRARY)   -- key
 * Value Length          (4 bytes)     -- Length of the next byte sequence containing value
 * Value                 (ARBITRARY)   -- value
 */
std::vector<uint8_t> serialize_wal_record(const WalRecord& record);
// clang-format on

Result<WalRecord> deserialize_wal_record(const uint8_t* buffer, size_t length);

}  // namespace enigmadb::dazzle

#endif  // ENIGMA_DB_DAZZLE_WAL_RECORD_H
