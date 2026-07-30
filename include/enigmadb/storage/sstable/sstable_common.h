#ifndef ENIGMA_DB_SSTABLE_COMMON_H
#define ENIGMA_DB_SSTABLE_COMMON_H

#include <array>
#include <charconv>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "enigmadb/common/error.h"
#include "enigmadb/common/result.h"

namespace enigmadb::storage::sstable {

/// @brief Maximum data block size in bytes before flushing to disk.
/// @todo Could be derived from the OS page size at runtime.
constexpr size_t MAX_PAGING_SIZE_BYTES = 4096;

/// @brief Current SSTable on-disk format version.
constexpr size_t SSTABLE_FORMAT_VERSION = 3;

/// @brief Size of the magic identifier in the footer, in bytes.
static constexpr size_t MAGIC_SIZE = 8;

/// @brief Magic bytes written to the footer to identify a valid SSTable file.
static constexpr std::array<char, 8> MAGIC = {'E', 'N', 'I', 'G',
                                              'S', 'S', 'T', 'B'};

/// @brief Convenience alias for result types used throughout the SSTable layer.
template <typename T>
using SSTExpectResult = common::ExpectResult<T, common::Error>;

/**
 * @brief An 8 byte struct that tracks an SSTable independently via an
 *        auto-incrementing id.
 */
struct SSTableId {
    uint64_t value;  ///< Id of the sstable

    bool operator==(const SSTableId& oth) const { return value == oth.value; }
    bool operator<(const SSTableId& oth) const { return value < oth.value; }
};

struct SSTableIdComparator {
    bool operator()(const SSTableId& a, const SSTableId& b) const {
        return a.value > b.value;
    }
};

inline std::string sstable_filename(SSTableId id) {
    std::stringstream ss;
    ss << "sst_" << std::setfill('0') << std::setw(8) << id.value << ".db";
    return ss.str();
}

inline SSTableId parse_sstable_filename(std::string_view filename) {
    constexpr std::string_view prefix = "sst_";
    constexpr std::string_view suffix = ".db";
    uint64_t value = 0;

    if (filename.size() <= prefix.size() + suffix.size() ||
        filename.substr(0, prefix.size()) != prefix ||
        filename.substr(filename.size() - suffix.size()) != suffix) {
        return SSTableId{value};
    }

    auto part = filename.substr(
        prefix.size(), filename.size() - prefix.size() - suffix.size());
    auto [ptr, ec] =
        std::from_chars(part.data(), part.data() + part.size(), value);
    if (ptr != part.data() + part.size()) {
        return SSTableId{0};
    }

    /* TODO: Server logger and need to replace these */
    if (ec == std::errc::invalid_argument) {
        std::cout << "This is not a number.\n";
    } else if (ec == std::errc::result_out_of_range) {
        std::cout << "This number is larger than an int.\n";
    }

    return SSTableId{value};
}

/**
 * @brief An entry in the SSTable's in-memory index, mapping a data
 *        block to its position in the file.
 *
 * One IndexEntry is created per data block and written to the index
 * block by SSTableWriter::finish().
 */
struct IndexEntry {
    std::vector<uint8_t> first_key;  ///< First (smallest) key in the block.
    size_t block_offset;  ///< Byte offset of the block from file start.
    size_t block_size;    ///< Size of the block in bytes.
};

struct MinimalSSTableFooter {
    uint64_t index_block_offset;
    uint32_t index_block_size;
    uint64_t filter_block_offset;
    uint32_t filter_block_size;
    uint32_t entry_count;
    uint16_t format_version;  ///< Footer format stored as 2 bytes
    uint64_t highest_sequence;
};

}  // namespace enigmadb::storage::sstable

#endif  // ENIGMA_DB_SSTABLE_WRITER_H
