#include "enigmadb/bloom_filter.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

#include "enigmadb/hash.h"

namespace enigmadb {

BloomFilter::BloomFilter(size_t expected_keys, double false_positive_rate) {
    assert(expected_keys > 0);
    assert(false_positive_rate > 0.0 && false_positive_rate < 1.0);

    auto ln2 = std::log(2.0);

    // bit_count = -(expected_keys * ln(false_positive_rate)) / (ln(2)^2)
    auto m =
        -(static_cast<double>(expected_keys) * std::log(false_positive_rate)) /
        (ln2 * ln2);
    bit_count_ =
        ((static_cast<size_t>(m) + 7) / 8) * 8;  // round up to byte boundary

    // num_hashes = (bit_count / expected_key) * ln(2)
    auto k = static_cast<size_t>(
        (static_cast<double>(bit_count_) / static_cast<double>(expected_keys)) *
        ln2);
    num_hashes_ = k;

    num_hashes_ = std::max(size_t{1}, std::min(num_hashes_, size_t{30}));

    bit_array_.resize((bit_count_ + 7) / 8,
                      0);  // round up to full bytes, zero-initialized
}

void BloomFilter::add(const std::vector<uint8_t>& key) {
    auto h1 = Hash(key.data(), key.size(), SEED_1);
    auto h2 = Hash(key.data(), key.size(), SEED_2);

    for (size_t i = 0; i < num_hashes_; i++) {
        size_t bit_pos = (h1 + i * (h2 | 1)) % bit_count_;
        size_t byte_idx = bit_pos / 8;
        size_t bit_offset = bit_pos % 8;
        bit_array_[byte_idx] |= (1 << bit_offset);
    }
}

bool BloomFilter::may_contain(const std::vector<uint8_t>& key) const {
    auto h1 = Hash(key.data(), key.size(), SEED_1);
    auto h2 = Hash(key.data(), key.size(), SEED_2);

    for (size_t i = 0; i < num_hashes_; i++) {
        size_t bit_pos = (h1 + i * (h2 | 1)) % bit_count_;
        size_t byte_idx = bit_pos / 8;
        size_t bit_offset = bit_pos % 8;
        if (!(bit_array_[byte_idx] & (1 << bit_offset))) {
            return false;
        }
    }
    return true;
}

const std::vector<uint8_t>& BloomFilter::data() const { return bit_array_; }

uint8_t BloomFilter::num_hashes() const {
    return static_cast<uint8_t>(num_hashes_);
}

size_t BloomFilter::size_bytes() const { return bit_array_.size(); }

BloomFilter BloomFilter::from_keys(
    const std::vector<std::vector<uint8_t>>& keys, double false_positive_rate) {
    BloomFilter filter(keys.size(), false_positive_rate);
    for (const auto& key : keys) {
        filter.add(key);
    }
    return filter;
}

}  // namespace enigmadb
