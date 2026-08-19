/**
 * @file key_encoding.h
 * @brief Composite key encoding for the EnigmaDB storage layer.
 *
 * @author frostzt
 * @date 2026-04-05
 */

#ifndef ENIGMA_DB_CATALOG_KEY_ENCODING_H
#define ENIGMA_DB_CATALOG_KEY_ENCODING_H

#include <cstdint>
#include <span>
#include <vector>

#include "enigmadb/base.h"

namespace enigmadb::catalog {

struct CompositeKey {
    std::vector<uint8_t> partition_key;
    std::vector<uint8_t> clustering_key;
    std::vector<uint8_t> column_name;
};

std::vector<uint8_t> encode_composite_key(std::span<const uint8_t> partition_key,
                                          std::span<const uint8_t> clustering_key,
                                          std::span<const uint8_t> column_name);

Result<CompositeKey> decode_composite_key(std::span<const uint8_t> encoded);

}  // namespace enigmadb::catalog

#endif  // ENIGMA_DB_CATALOG_KEY_ENCODING_H
