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

#include "enigmadb/storage/key.h"

namespace enigmadb::dazzle {

/// @brief The total size of the footer block
constexpr size_t FOOTER_SIZE = 64;

constexpr size_t MAGIC_BYTES_OFFSET = 50;
constexpr size_t FOOTER_CHECKSUM_OFFSET = 46;

/// @brief Maximum data block size in bytes before flushing to disk.
/// @todo Could be derived from the OS page size at runtime.
constexpr size_t MAX_PAGING_SIZE_BYTES = 4096;

/// @brief Current SSTable on-disk format version.
constexpr size_t SSTABLE_FORMAT_VERSION = 4;

/// @brief Size of the magic identifier in the footer, in bytes.
static constexpr size_t MAGIC_SIZE = 8;

/// @brief Magic bytes written to the footer to identify a valid SSTable file.
static constexpr std::array<char, 8> MAGIC = {'E', 'N', 'I', 'G',
                                              'S', 'S', 'T', 'B'};

/**
 * @brief An 8 byte struct that tracks an SSTable independently via an
 *        auto-incrementing id.
 */
struct SSTableId {
    uint64_t value;  ///< Id of the sstable

    auto operator<=>(const SSTableId& oth) const = default;
    bool operator==(const SSTableId& oth) const { return value == oth.value; }
};

/**
 * @brief Struct representing metadata for a sstable
 */
struct SSTableMeta {
    SSTableId id;
    uint64_t size_bytes;
    uint64_t entry_count;
    uint64_t max_sequence;
};

struct SSTableIdComparator {
    bool operator()(const SSTableId& a, const SSTableId& b) const {
        return a.value < b.value;
    }
};

inline std::string sstable_filename(SSTableId id) {
    std::stringstream ss;
    ss << "sst_" << std::setfill('0') << std::setw(8) << id.value << ".db";
    return ss.str();
}

inline std::string sst_path(std::string data_dir, uint64_t seq) {
    std::stringstream ss;
    ss << data_dir << "/sst/" << sstable_filename(SSTableId{seq});
    return ss.str();
}

inline SSTableId parse_sstable_filename(std::string_view path) {
    constexpr std::string_view prefix = "sst_";
    constexpr std::string_view suffix = ".db";

    auto last_slash = path.find_last_of("/\\");
    std::string_view filename = (last_slash == std::string_view::npos)
                                    ? path
                                    : path.substr(last_slash + 1);

    if (filename.size() <= prefix.size() + suffix.size() ||
        filename.substr(0, prefix.size()) != prefix ||
        filename.substr(filename.size() - suffix.size()) != suffix) {
        return SSTableId{0};
    }

    auto num_part = filename.substr(
        prefix.size(), filename.size() - prefix.size() - suffix.size());

    uint64_t value = 0;
    auto [ptr, ec] = std::from_chars(num_part.data(),
                                     num_part.data() + num_part.size(), value);

    /* TODO: Server logs */
    if (ec == std::errc::invalid_argument) {
        std::cout << "This is not a number.\n";
        return SSTableId{0};
    } else if (ec == std::errc::result_out_of_range) {
        std::cout << "This number is larger than uint64_t.\n";
        return SSTableId{0};
    }

    if (ptr != num_part.data() + num_part.size()) {
        return SSTableId{0};
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
    storage::Key first_key;  ///< First (smallest) key in the block.
    size_t block_offset;     ///< Byte offset of the block from file start.
    size_t block_size;       ///< Size of the block in bytes.
};

struct SSTFooter {
    uint64_t index_block_offset;
    uint32_t index_block_size;
    uint64_t filter_block_offset;
    uint32_t filter_block_size;
    uint32_t entry_count;
    uint16_t format_version;  ///< Footer format stored as 2 bytes
    uint64_t highest_sequence;
    uint64_t size_bytes;
};

}  // namespace enigmadb::dazzle

#endif  // ENIGMA_DB_SSTABLE_WRITER_H
