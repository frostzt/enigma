/**
 * @file compaction.h
 * @brief Common compaction implementation
 *
 * @author frostzt
 * @date 2026-06-03
 */

#ifndef ENIGMA_DB_COMPACTION_H
#define ENIGMA_DB_COMPACTION_H

#include <vector>

#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"

namespace enigmadb::dazzle {

struct SizeTieredConfig {
    size_t min_merge_width_;  ///< Minimum number of files required to
                              ///< trigger compaction
    size_t max_merge_width_;  ///< Maximum number of files to consider while
                              ///< compacting
};

using CompactionConfig = std::variant<SizeTieredConfig>;

class Compactor {
   private:
    io::IOEngine& engine_;
    const std::string data_dir_;

    Compactor(io::IOEngine& engine, std::string data_dir)
        : engine_(engine), data_dir_(std::move(data_dir)) {}

    /* @TODO: Need to figure out a better architecture here cause these
     *        functions do duplicate work across sstable parts */

   public:
    static Compactor create(io::IOEngine& engine, const std::string& data_dir);

    Result<SSTableId> do_size_tiered_compact(
        const std::vector<SSTableId>& inputs, const uint64_t next_sst_seq,
        bool is_full_compaction);
};

}  // namespace enigmadb::dazzle

#endif  // ENIGMA_DB_COMPACTION_H
