/*
 * bloom_filter.h -- :)
 *
 * Author: frostzt
 * Date: 2026-05-30
 */

#ifndef ENIGMA_DB_BLOOM_FILTER_H
#define ENIGMA_DB_BLOOM_FILTER_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "enigmadb/storage/key.h"

namespace enigmadb {

constexpr auto SEED_1 = 0xbc9f1d34;
constexpr auto SEED_2 = 0x6c8e9cf5;

class BloomFilter {
   private:
    size_t bit_count_;
    size_t num_hashes_;
    std::vector<uint8_t> bit_array_;

   public:
    // Construct with expected number of keys and desired false positive rate
    BloomFilter(size_t expected_keys, double false_positive_rate);

    // Construct from serialized data (for loading from SSTable)
    BloomFilter(std::vector<uint8_t> bit_array, uint8_t num_hash_functions)
        : num_hashes_(num_hash_functions), bit_array_(std::move(bit_array)) {
        bit_count_ = bit_array_.size() * 8;  // convert bytes back to bits
    }

    void add(const storage::Key& key);
    bool may_contain(const storage::Key& key) const;

    static BloomFilter from_keys(const std::vector<storage::Key>& keys, double false_positive_rate);

    // For serializing into SSTable
    const std::vector<uint8_t>& data() const;
    uint8_t num_hashes() const;
    size_t size_bytes() const;
};

}  // namespace enigmadb

#endif  // ENIGMA_DB_BLOOM_FILTER_H
