/**
 * @file key_encoding.h
 * @brief Composite key encoding for the EnigmaDB storage layer.
 *
 * Encodes a (partition_key, clustering_key, column_name) triple into a
 * single contiguous byte sequence for use as a lookup key in ordered
 * data structures. The wire format is:
 *
 * @code
 * | part_key_len (4B) | part_key | clus_key_len (4B) | clus_key | col_len (4B)
 * | column |
 * @endcode
 *
 * All lengths are encoded as big-endian uint32_t values.
 *
 * @author frostzt
 * @date 2026-04-05
 */

#ifndef ENIGMA_DB_KEY_ENCODING_H
#define ENIGMA_DB_KEY_ENCODING_H

#include <stdint.h>

#include <string>
#include <vector>

namespace enigmadb::storage {

/**
 * @brief Encodes a partition key, clustering key, and column name into
 *        a single composite key byte sequence.
 *
 * Each component is prefixed with its 4-byte big-endian length, then
 * followed by its raw bytes, producing the layout:
 *
 * @code
 * [part_key_len][part_key_bytes][clus_key_len][clus_key_bytes][col_len][col_bytes]
 * @endcode
 *
 * @param[in] partition_key   Raw bytes of the partition key.
 * @param[in] clustering_key  Raw bytes of the clustering key.
 * @param[in] column_name     Column name (encoded as raw UTF-8 bytes).
 * @return A self-contained composite key suitable for ordered lookups.
 */
std::vector<uint8_t> encode_composite_key(
    const std::vector<uint8_t>& partition_key,
    const std::vector<uint8_t>& clustering_key, const std::string& column_name);

void decode_composite_key(const std::vector<uint8_t>& compkey,
                          std::vector<uint8_t>& pkey,
                          std::vector<uint8_t>& ckey, std::string& cname);

/**
 * @brief Strict weak ordering comparator for encoded composite keys.
 *
 * Comparison proceeds in three stages, each decoding the next component
 * from both keys:
 *   1. Partition key — compared lexicographically.
 *   2. Clustering key — compared lexicographically (only if partition
 *      keys are equal).
 *   3. Column name — compared lexicographically (only if both prior
 *      components are equal).
 *
 * Keys that are equal across all three components compare as equivalent
 * (returns false for both a < b and b < a).
 *
 * Intended for use as the comparator in ordered containers:
 * @code
 * std::map<std::vector<uint8_t>, Value, CompositeKeyComparator> memtable;
 * @endcode
 *
 * @note Both operands must be well-formed composite keys produced by
 *       encode_composite_key(); behavior is undefined otherwise.
 *
 * @todo Could short-circuit with a single memcmp on the raw encoded
 *       bytes when keys are identical, or eliminate decoding entirely
 *       by switching to a null-terminated encoding scheme where raw
 *       byte comparison yields correct ordering.
 */
struct CompositeKeyComparator {
    bool operator()(const std::vector<uint8_t>& a,
                    const std::vector<uint8_t>& b) const;
};

}  // namespace enigmadb::storage

#endif  // ENIGMA_DB_KEY_ENCODING_H
