#include "enigmadb/storage/dazzle_db/compaction/compaction_policy.h"

#include <algorithm>
#include <cassert>
#include <optional>
#include <vector>

#include "enigmadb/storage/dazzle_db/compaction/compaction.h"
#include "enigmadb/storage/dazzle_db/compaction/tombstone_gc.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"

namespace enigmadb::dazzle {

std::optional<CompactionCandidate> FullCompactionPolicy::pick(
    const std::vector<const SSTableMeta*>& live) {
    if (live.size() == 0) {
        return std::optional<CompactionCandidate>(std::nullopt);
    }

    std::vector<SSTableId> inputs;
    inputs.reserve(live.size());
    for (auto meta : live) {
        inputs.push_back(meta->id);
    }

    /* Picks all the inputs and drops every tombstone */
    return std::optional<CompactionCandidate>(
        CompactionCandidate{inputs, true});
}

/* the core idea here is that we use the bucket to group file size_bytes
 * between an inclusive [bucket_low, bucket_high] of curr bucket average */
std::optional<CompactionCandidate> SizeTieredCompactionPolicy::pick(
    const std::vector<const SSTableMeta*>& live) {
    if (live.size() == 0) {
        return std::optional<CompactionCandidate>(std::nullopt);
    }

    assert(max_merge_width_ >= min_merge_width_);

    /* sort by file size_bytes */
    std::vector<const SSTableMeta*> sorted_live = live;
    std::sort(sorted_live.begin(), sorted_live.end(),
              [](const SSTableMeta* a, const SSTableMeta* b) -> bool {
                  return a->size_bytes < b->size_bytes;
              });

    /* sort metas into buckets */
    struct bucket {
        SSTableId oldest;
        double average;
        std::vector<const SSTableMeta*> metas;
    };

    std::vector<bucket> buckets;
    buckets.reserve(sorted_live.size());

    buckets.push_back(bucket{sorted_live[0]->id,
                             static_cast<double>(sorted_live[0]->size_bytes),
                             {sorted_live[0]}});
    size_t current_bucket = 0;
    double current_sum = sorted_live[0]->size_bytes;
    for (size_t i = 1; i < sorted_live.size(); i++) {
        auto c_avg = buckets[current_bucket].average;
        auto sz = sorted_live[i]->size_bytes;

        /* the current file is [bucket_low, bucket_high] of current bucket's
         * average */
        if (sz >= (bucket_low_ * c_avg) && sz <= (bucket_high_ * c_avg)) {
            current_sum += sorted_live[i]->size_bytes;
            buckets[current_bucket].metas.push_back(sorted_live[i]);
            buckets[current_bucket].average =
                current_sum / buckets[current_bucket].metas.size();
            if (sorted_live[i]->id < buckets[current_bucket].oldest) {
                buckets[current_bucket].oldest = sorted_live[i]->id;
            }
        } else {
            current_bucket++;
            current_sum = sorted_live[i]->size_bytes;
            buckets.push_back(
                bucket{sorted_live[i]->id,
                       static_cast<double>(sorted_live[i]->size_bytes),
                       {sorted_live[i]}});
        }
    }

    /* find bucket >= min_merge_width items */
    std::vector<bucket> found_buckets;
    for (const auto& bucket : buckets) {
        if (bucket.metas.size() >= min_merge_width_) {
            found_buckets.push_back(bucket);
        }
    }

    if (found_buckets.size() == 0) {
        return std::optional<CompactionCandidate>(std::nullopt);
    }

    std::sort(found_buckets.begin(), found_buckets.end(),
              [](const bucket& a, const bucket& b) -> bool {
                  return a.oldest < b.oldest;
              });

    auto& chosen = found_buckets[0].metas;
    std::sort(chosen.begin(), chosen.end(),
              [](const SSTableMeta* a, const SSTableMeta* b) {
                  return a->id < b->id;
              });

    /* finalize the buckets */
    std::vector<SSTableId> inputs;
    inputs.reserve(max_merge_width_);
    for (const auto& m : found_buckets[0].metas) {
        inputs.push_back(m->id);
        if (inputs.size() >= max_merge_width_) break;
    }

    std::vector<SSTableId> live_inputs;
    live_inputs.reserve(live.size());
    for (const auto& m : live) {
        live_inputs.push_back(m->id);
    }
    std::sort(live_inputs.begin(), live_inputs.end());

    auto drop_tombstones = can_drop_tombstones(live_inputs, inputs);

    return std::optional<CompactionCandidate>(
        CompactionCandidate{inputs, drop_tombstones});
}

}  // namespace enigmadb::dazzle
