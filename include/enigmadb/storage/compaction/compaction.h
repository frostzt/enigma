/**
 * @file compaction.h
 * @brief
 * @note Test-only — do not use in production code.
 *
 * @author frostzt
 * @date 2026-06-03
 */

#ifndef ENIGMA_DB_COMPACTION_H
#define ENIGMA_DB_COMPACTION_H

#include <vector>

#include "enigmadb/common/error.h"
#include "enigmadb/common/result.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/sstable/sstable_common.h"

namespace enigmadb::storage::compaction {

using DoCompactResult = common::ExpectResult<void, common::Error>;

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

    DoCompactResult do_compact(const std::vector<sstable::SSTableId>& inputs,
                               bool is_full_compaction);
};

}  // namespace enigmadb::storage::compaction

#endif  // ENIGMA_DB_COMPACTION_H
