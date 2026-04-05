#include <enigmadb/storage/key_encoding.h>

#include <algorithm>

#include "enigmadb/common/encoding.h"

namespace enigmadb::storage {

std::vector<uint8_t> encode_composite_key(
    const std::vector<uint8_t>& partition_key,
    const std::vector<uint8_t>& clustering_key,
    const std::string& column_name) {
    size_t total = 4 + partition_key.size() + 4 + clustering_key.size() + 4 +
                   column_name.size();
    std::vector<uint8_t> out(total);
    size_t offset = 0;

    /* encode partition key */
    offset = common::encode_uint32(partition_key.size(), out.data(), offset);
    offset = common::encode_bytes(partition_key.data(), partition_key.size(),
                                  out.data(), offset);

    /* encode clustering key */
    offset = common::encode_uint32(clustering_key.size(), out.data(), offset);
    offset = common::encode_bytes(clustering_key.data(), clustering_key.size(),
                                  out.data(), offset);

    /* encode column */
    offset = common::encode_uint32(column_name.size(), out.data(), offset);
    offset = common::encode_bytes(column_name.data(), column_name.size(),
                                  out.data(), offset);

    return out;
}

// TODO: Could short-circuit by comparing the raw encoded bytes
// directly with a single memcmp first (if the entire encoded
// key is identical, they're equal no decoding needed), and
// could avoid decoding at all if we switched to the null-terminated
// encoding scheme where raw byte comparison gives correct ordering.

bool CompositeKeyComparator::operator()(const std::vector<uint8_t>& a,
                                        const std::vector<uint8_t>& b) const {
    size_t offset_a = 0;
    size_t offset_b = 0;

    /* --- COMPARE PARTITIONING KEY --- */
    /* Decode partition_key from both a and b */
    auto pkeylen_a = common::decode_uint32(a.data(), offset_a);
    offset_a += 4;
    auto pkeylen_b = common::decode_uint32(b.data(), offset_b);
    offset_b += 4;

    /* Compare partition_key contents if not equal → return which is smaller */
    /* a < b */
    if (std::lexicographical_compare(
            a.data() + offset_a, a.data() + offset_a + pkeylen_a,
            b.data() + offset_b, b.data() + offset_b + pkeylen_b)) {
        return true;
    }

    /* b < a */
    if (std::lexicographical_compare(
            b.data() + offset_b, b.data() + offset_b + pkeylen_b,
            a.data() + offset_a, a.data() + offset_a + pkeylen_a)) {
        return false;
    }

    offset_a += pkeylen_a;
    offset_b += pkeylen_b;

    /* --- COMPARE CLUSTERING KEY --- */
    /* Decode clustering_key from both a and b */
    auto ckeylen_a = common::decode_uint32(a.data(), offset_a);
    offset_a += 4;
    auto ckeylen_b = common::decode_uint32(b.data(), offset_b);
    offset_b += 4;

    /* Compare clustering_key contents if not equal → return which is smaller */
    /* a < b */
    if (std::lexicographical_compare(
            a.data() + offset_a, a.data() + offset_a + ckeylen_a,
            b.data() + offset_b, b.data() + offset_b + ckeylen_b)) {
        return true;
    }

    /* b < a */
    if (std::lexicographical_compare(
            b.data() + offset_b, b.data() + offset_b + ckeylen_b,
            a.data() + offset_a, a.data() + offset_a + ckeylen_a)) {
        return false;
    }

    offset_a += ckeylen_a;
    offset_b += ckeylen_b;

    /* --- COMPARE COLUMN --- */
    /* Decode column from both a and b */
    auto col_len_a = common::decode_uint32(a.data(), offset_a);
    offset_a += 4;
    auto col_len_b = common::decode_uint32(b.data(), offset_b);
    offset_b += 4;

    /* Compare col contents if not equal → return which is smaller */
    /* a < b */
    if (std::lexicographical_compare(
            a.data() + offset_a, a.data() + offset_a + col_len_a,
            b.data() + offset_b, b.data() + offset_b + col_len_b)) {
        return true;
    }

    /* b < a */
    if (std::lexicographical_compare(
            b.data() + offset_b, b.data() + offset_b + col_len_b,
            a.data() + offset_a, a.data() + offset_a + col_len_a)) {
        return false;
    }
    return false;
}

}  // namespace enigmadb::storage
