/**
 * @file compaction_policy.h
 * @brief Compaction policy defines what compaction algorithm to execute and
 * generates a Compaction task
 *
 * @author frostzt
 * @date 2026-08-04
 */

#ifndef ENIGMA_DB_COMPACTION_POLICY_H
#define ENIGMA_DB_COMPACTION_POLICY_H

#include <optional>
#include <string_view>
#include <vector>

#include "enigmadb/storage/dazzle_db/compaction/compaction.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"

namespace enigmadb::dazzle {

class CompactionPolicy {
   public:
    virtual ~CompactionPolicy() = default;
    virtual std::optional<CompactionCandidate> pick(const std::vector<SSTableMeta>& live) = 0;
    virtual std::string_view name() = 0;
};

class FullCompactionPolicy : public CompactionPolicy {
    std::optional<CompactionCandidate> pick(const std::vector<SSTableMeta>& live) override;

    std::string_view name() override { return "full_compaction_policy"; };
};

class SizeTieredCompactionPolicy : public CompactionPolicy {
   public:
    SizeTieredCompactionPolicy(size_t min_merge_width, size_t max_merge_width, double bucket_low = 0.5,
                               double bucket_high = 1.5)
        : min_merge_width_(min_merge_width),
          max_merge_width_(max_merge_width),
          bucket_low_(bucket_low),
          bucket_high_(bucket_high) {};

    std::string_view name() override { return "size_tiered_compaction_policy"; };

    std::optional<CompactionCandidate> pick(const std::vector<SSTableMeta>& live) override;

   private:
    size_t min_merge_width_, max_merge_width_;
    double bucket_low_, bucket_high_;
};

}  // namespace enigmadb::dazzle

#endif  // ENIGMA_DB_COMPACTION_POLICY_H
