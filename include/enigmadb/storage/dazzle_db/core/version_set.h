#ifndef ENIGMADB_DAZZLEDB_CORE_VERSION_SET_H_
#define ENIGMADB_DAZZLEDB_CORE_VERSION_SET_H_

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

#include "enigmadb/base.h"
#include "enigmadb/storage/dazzle_db/core/version.h"
#include "enigmadb/storage/dazzle_db/core/version_edit.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"

namespace enigmadb::dazzle {

class VersionSet {
   public:
    VersionSet(std::map<SSTableId, SSTableMeta, SSTableIdComparator> sst_meta)
        : current_version_(std::make_shared<const Version>(std::move(sst_meta))) {
        live_versions_.push_back(current_version_);
    }

    ~VersionSet() = default;

    std::shared_ptr<const Version> get_current() const {
        std::lock_guard<std::mutex> lk(mu_);
        return current_version_;
    };

    /* TODO: Manifest changes pending */
    Result<std::vector<SSTableId>> apply(VersionEdit edit) {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto& id : edit.removed) {
            if (current_version_->files().find(id) == current_version_->files().end()) {
                return Result<std::vector<SSTableId>>::err(Error::stale_version("VersionEdit already consumed"));
            }
        }

        auto new_map = current_version_->files();

        /* remove all the removed files */
        for (const auto& id : edit.removed) {
            new_map.erase(id);
        }

        /* add the added files into the map */
        for (const auto& meta : edit.added) {
            new_map[meta.id] = meta;
        }

        auto next_version = std::make_shared<Version>(std::move(new_map));
        auto published = append_version(next_version, edit.removed);

        return Result<std::vector<SSTableId>>::ok(published);
    }

   private:
    std::shared_ptr<const Version> current_version_;
    std::vector<std::shared_ptr<const Version>> live_versions_;
    std::set<SSTableId> pending_obsolete_ids_;
    mutable std::mutex mu_;

    std::vector<SSTableId> append_version(std::shared_ptr<Version> new_version,
                                          std::vector<SSTableId> obsolete_ids = {}) {
        current_version_ = std::move(new_version);
        live_versions_.push_back(current_version_);

        if (!obsolete_ids.empty()) {
            pending_obsolete_ids_.insert(obsolete_ids.begin(), obsolete_ids.end());
        }

        return purge_obsolete_files();
    }

    std::vector<SSTableId> purge_obsolete_files() {
        /* remove versions that no longer have a read iterator attached to them */
        live_versions_.erase(std::remove_if(live_versions_.begin(), live_versions_.end(),
                                            /* 1 here means only VersionSet is the one owning it */
                                            [](const std::shared_ptr<const Version>& v) { return v.use_count() == 1; }),
                             live_versions_.end());

        /* ssts needed by surviving versions */
        std::set<SSTableId> live_ids;
        for (const auto& [id, _] : current_version_->files()) {
            live_ids.insert(id);
        }

        for (const auto& ver : live_versions_) {
            for (const auto& [id, _] : ver->files()) {
                live_ids.insert(id);
            }
        }

        std::vector<SSTableId> reclaimable;
        for (auto it = pending_obsolete_ids_.begin(); it != pending_obsolete_ids_.end();) {
            if (live_ids.count(*it) == 0) {
                reclaimable.push_back(*it);
                it = pending_obsolete_ids_.erase(it);
            } else {
                ++it;
            }
        }

        return reclaimable;
    }
};

}  // namespace enigmadb::dazzle

#endif  //  ENIGMADB_DAZZLEDB_CORE_VERSION_SET_H_
