#ifndef ENIGMA_DB_DAZZLE_VERSION_SET_H
#define ENIGMA_DB_DAZZLE_VERSION_SET_H

#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "enigmadb/storage/dazzle_db/core/version.h"

namespace fs = std::filesystem;

namespace enigmadb::dazzle {

class VersionSet {
   public:
    VersionSet() : current_version_(std::make_shared<const Version>()) {}

    ~VersionSet() = default;

    std::shared_ptr<const Version> get_current() {
        std::lock_guard<std::mutex> lk(mu_);
        return current_version_;
    };

    void append_version(std::shared_ptr<Version> new_version, std::vector<std::string> obsolete_files = {}) {
        std::lock_guard<std::mutex> lock(mu_);

        current_version_ = std::move(new_version);
        live_versions_.push_back(current_version_);

        if (!obsolete_files.empty()) {
            pending_obsolete_files_.insert(obsolete_files.begin(), obsolete_files.end());
        }

        purge_obsolete_files();
    }

   private:
    std::shared_ptr<const Version> current_version_;
    std::vector<std::shared_ptr<const Version>> live_versions_;
    std::set<std::string> pending_obsolete_files_;
    std::mutex mu_;

    void purge_obsolete_files() {
        /* remove versions that no longer have a read iterator attached to them */
        live_versions_.erase(std::remove_if(live_versions_.begin(), live_versions_.end(),
                                            /* 1 here means only VersionSet is the one owning it */
                                            [](const std::shared_ptr<const Version>& v) { return v.use_count() == 1; }),
                             live_versions_.end());

        /* ssts needed by surviving versions */
        std::set<std::string_view> live_files;
        for (const auto& ver : live_versions_) {
            for (const auto& [_, reader] : ver->sst_readers) {
                live_files.insert(reader->get_path());
            }
        }

        for (auto it = pending_obsolete_files_.begin(); it != pending_obsolete_files_.end();) {
            if (live_files.find(*it) == live_files.end()) {
                std::error_code ec;
                fs::remove(*it, ec);
                if (!ec) {
                    it = pending_obsolete_files_.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }
};

}  // namespace enigmadb::dazzle

#endif  //  ENIGMA_DB_DAZZLE_VERSION_SET_H
