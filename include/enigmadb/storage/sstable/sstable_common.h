#ifndef ENIGMA_DB_SSTABLE_COMMON_H
#define ENIGMA_DB_SSTABLE_COMMON_H

#include <array>
#include <vector>

#include "enigmadb/common/error.h"
#include "enigmadb/common/result.h"

namespace enigmadb::storage::sstable {

/// @brief Maximum data block size in bytes before flushing to disk.
/// @todo Could be derived from the OS page size at runtime.
constexpr size_t MAX_PAGING_SIZE_BYTES = 4096;

/// @brief Current SSTable on-disk format version.
constexpr size_t SSTABLE_FORMAT_VERSION = 1;

/// @brief Size of the magic identifier in the footer, in bytes.
static constexpr size_t MAGIC_SIZE = 8;

/// @brief Magic bytes written to the footer to identify a valid SSTable file.
static constexpr std::array<char, 8> MAGIC = {'E', 'N', 'I', 'G',
                                              'S', 'S', 'T', 'B'};

/// @brief Convenience alias for result types used throughout the SSTable layer.
template <typename T>
using SSTExpectResult = common::ExpectResult<T, common::Error>;

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

}  // namespace enigmadb::storage::sstable

#endif  // ENIGMA_DB_SSTABLE_WRITER_H
