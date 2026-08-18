#include "enigmadb/catalog/key_encoding.h"

#include <sys/types.h>

#include <cstdint>
#include <span>
#include <vector>

#include "enigmadb/base.h"
#include "enigmadb/error.h"

namespace enigmadb::catalog {

std::vector<uint8_t> encode_composite_key(std::span<const uint8_t> partition_key,
                                          std::span<const uint8_t> clustering_key,
                                          std::span<const uint8_t> column_name) {
    std::vector<uint8_t> out;

    /* pre-emptively calculate worst-case allocation required */
    out.reserve((partition_key.size() * 2) + (clustering_key.size() * 2) + (column_name.size() * 2) +
                /* 2 pterm + 2 cterm + 2colterm */ 6);

    /* copy escaped partition key */
    for (uint8_t byte : partition_key) {
        out.push_back(byte);
        if (byte == 0x00) {
            out.push_back(0xFF);
        }
    }

    /* write terminator for partition */
    out.push_back(0x00);
    out.push_back(0x01);

    /* copy escaped clustering key */
    for (uint8_t byte : clustering_key) {
        out.push_back(byte);
        if (byte == 0x00) {
            out.push_back(0xFF);
        }
    }

    /* write terminator for clustering */
    out.push_back(0x00);
    out.push_back(0x01);

    /* copy column name */
    for (uint8_t byte : column_name) {
        out.push_back(byte);
        if (byte == 0x00) {
            out.push_back(0xFF);
        }
    }

    /* write terminator for column name */
    out.push_back(0x00);
    out.push_back(0x01);

    return out;
}

Result<CompositeKey> decode_composite_key(std::span<const uint8_t> encoded) {
    std::vector<uint8_t> partition_key;
    std::vector<uint8_t> clustering_key;
    std::vector<uint8_t> column_name;

    auto it = encoded.begin();
    auto end = encoded.end();

    auto decode_field = [&](std::vector<uint8_t>& target) -> bool {
        while (it != end) {
            uint8_t b = *it++;

            if (b == 0x00) {
                /* malformed */
                if (it == end) return false;

                uint8_t next = *it++;
                if (next == 0xFF) {
                    target.push_back(0x00);
                } else if (next == 0x01) {
                    return true;
                } else {
                    /* malformed */
                    return false;
                }
            } else {
                target.push_back(b);
            }
        }

        return false; /* end before delim */
    };

    /* decode partition key */
    if (!decode_field(partition_key)) {
        return Result<CompositeKey>::err(Error{ErrorCode::READ_ERR, "Malformed partition key"});
    }

    /* decode clustering key */
    if (!decode_field(clustering_key)) {
        return Result<CompositeKey>::err(Error{ErrorCode::READ_ERR, "Malformed clustering key"});
    }

    /* decode column name */
    if (!decode_field(column_name)) {
        return Result<CompositeKey>::err(Error{ErrorCode::READ_ERR, "Malformed column name"});
    }

    return Result<CompositeKey>::ok(
        CompositeKey{std::move(partition_key), std::move(clustering_key), std::move(column_name)});
}

}  // namespace enigmadb::catalog
