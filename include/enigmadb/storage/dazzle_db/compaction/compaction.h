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
#include "enigmadb/storage/dazzle_db/core/version.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"

namespace enigmadb::dazzle {

/// Describes a possible compaction task
struct CompactionCandidate {
    std::vector<SSTableId> inputs;
    bool can_drop_tombstone;
};

/// Describes a compaction task
struct CompactionTask {
    std::vector<SSTableId> inputs;
    SSTableId output_id;
    bool can_drop_tombstone;
};

class Compactor {
   private:
    io::IOEngine& engine_;
    const std::string data_dir_;

    Compactor(io::IOEngine& engine, std::string data_dir) : engine_(engine), data_dir_(std::move(data_dir)) {}

   public:
    static Compactor create(io::IOEngine& engine, const std::string& data_dir);

    Result<SSTableId> compact(std::shared_ptr<Version> snapshot, const std::vector<SSTableId>& inputs,
                              const uint64_t next_sst_seq, bool is_full_compaction);
};

}  // namespace enigmadb::dazzle

#endif  // ENIGMA_DB_COMPACTION_H
