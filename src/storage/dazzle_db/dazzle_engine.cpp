#include "enigmadb/storage/dazzle_db/dazzle_engine.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "enigmadb/base.h"
#include "enigmadb/error.h"
#include "enigmadb/log.h"
#include "enigmadb/storage/dazzle_db/compaction/compaction.h"
#include "enigmadb/storage/dazzle_db/compaction/tombstone_gc.h"
#include "enigmadb/storage/dazzle_db/memtable/memtable.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_reader.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_writer.h"
#include "enigmadb/storage/dazzle_db/wal/wal_reader.h"
#include "enigmadb/storage/dazzle_db/wal/wal_record.h"
#include "enigmadb/storage/dazzle_db/wal/wal_writer.h"
#include "enigmadb/storage/key.h"
#include "enigmadb/storage/value.h"
#include "enigmadb/utils.h"

namespace fs = std::filesystem;

namespace enigmadb::dazzle {

/* TODO: This is quite error prone but given we control file name should be good
 * for now */
uint64_t extract_num(const std::string& filename) {
    size_t start = filename.find("_") + 1;
    size_t end = filename.find(".");
    return std::stoll(filename.substr(start, end - start));
}

std::string Dazzle::wal_path(uint64_t seq) {
    std::stringstream ss;
    ss << get_wal_directory() << "/wal_" << std::setfill('0') << std::setw(8)
       << seq << ".log";
    return ss.str();
}

std::string Dazzle::sst_path(uint64_t seq) {
    std::stringstream ss;
    ss << get_sst_directory() << "/" << sstable_filename(SSTableId{seq});
    return ss.str();
}

Result<std::optional<InternalValue>> Dazzle::get_internal(
    const storage::Key& key) {
    if (auto found = active_memtable_.get(key); found.has_value()) {
        return Result<std::optional<InternalValue>>::ok(found);
    }

    for (auto it = sst_readers_.rbegin(); it != sst_readers_.rend(); ++it) {
        auto lookup = it->second->get(key);
        if (!lookup.has_value()) {
            return Result<std::optional<InternalValue>>::err(lookup.error());
        }
        if (lookup.value().has_value()) {
            return Result<std::optional<InternalValue>>::ok(lookup.value());
        }
    }

    return Result<std::optional<InternalValue>>::ok(std::nullopt);
}

Result<std::unique_ptr<Dazzle>> Dazzle::open(io::IOEngine& engine,
                                             const std::string& data_dir,
                                             const uint64_t memtable_size) {
    if (trim_string(data_dir) == "") {
        return Result<std::unique_ptr<Dazzle>>::err(
            Error{ErrorCode::BAD_CONFIG, "Data directory was not specified."});
    }

    /* create dirs if they don't exist */
    fs::path wal_dir_path = data_dir + "/wal";
    fs::path sst_dir_path = data_dir + "/sst";
    if (!fs::is_directory(wal_dir_path)) {
        if (!fs::create_directory(wal_dir_path)) {
            return Result<std::unique_ptr<Dazzle>>::err(
                Error{ErrorCode::UNEXPECTED_ERR, "failed to create wal dir"});
        }
    }

    if (!fs::is_directory(sst_dir_path)) {
        if (!fs::create_directory(sst_dir_path)) {
            return Result<std::unique_ptr<Dazzle>>::err(
                Error{ErrorCode::UNEXPECTED_ERR, "failed to create sst dir"});
        }
    }

    /* find all the sstable files */
    std::vector<fs::path> files;
    uint64_t highest_sst_seq = 0;
    for (const auto& entry : fs::directory_iterator(sst_dir_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".db") {
            files.push_back(entry.path());

            uint64_t seq = extract_num(entry.path().filename().string());
            highest_sst_seq = std::max(highest_sst_seq, seq);
        }
    }

    std::map<SSTableId, std::unique_ptr<SSTableReader>, SSTableIdComparator>
        sst_readers;

    uint64_t max_sst_sequence_found = 0;

    for (const auto& entry : files) {
        /* open an sstable reader */
        auto sstr = SSTableReader::create(engine, entry);
        if (!sstr.has_value()) {
            return Result<std::unique_ptr<Dazzle>>::err(sstr.error());
        }

        auto& reader = sstr.value();
        auto sstfooter = reader.get_footer();
        assert(sstfooter.has_value());

        auto& footer = sstfooter.value();
        if (footer.highest_sequence > max_sst_sequence_found) {
            max_sst_sequence_found = footer.highest_sequence;
        }

        sst_readers.insert(
            std::make_pair(SSTableId{extract_num(entry.filename().string())},
                           std::make_unique<SSTableReader>(std::move(reader))));
    }

    /* if wal files exist recover */
    std::vector<fs::path> wal_log_files;
    for (const auto& entry : fs::directory_iterator(wal_dir_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".log") {
            wal_log_files.push_back(entry.path());
        }
    }

    std::sort(wal_log_files.begin(), wal_log_files.end(),
              [](const fs::path& a, const fs::path& b) {
                  return extract_num(a.filename().string()) <
                         extract_num(b.filename().string());
              });

    Memtable mtable{memtable_size};
    uint64_t highest_wal_seq = 0;
    for (const auto& entry : wal_log_files) {
        /* find the highest sequence */
        auto filename = entry.filename();
        auto seq_found = extract_num(filename);
        if (seq_found > highest_wal_seq) {
            highest_wal_seq = seq_found;
        }
    }

    std::stringstream ss;
    ss << data_dir << "/wal/wal_" << std::setfill('0') << std::setw(8)
       << highest_wal_seq + 1 << ".log";
    auto wal_writer_res = WalWriter::create(engine, ss.str());
    if (!wal_writer_res.has_value()) {
        return Result<std::unique_ptr<Dazzle>>::err(wal_writer_res.error());
    }

    /* FIXME: Need to revisit this later right now its really bad here with the
     * sequences */
    auto storage_engine = std::unique_ptr<Dazzle>(new Dazzle(
        engine, data_dir, std::move(wal_writer_res.value()), memtable_size,
        std::move(mtable), std::move(sst_readers), highest_wal_seq + 1,
        highest_sst_seq + 1, max_sst_sequence_found + 1));

    /* try and recover */
    auto recover_result = storage_engine->recover();
    if (!recover_result.has_value()) {
        return Result<std::unique_ptr<Dazzle>>::err(recover_result.error());
    }

    return Result<std::unique_ptr<Dazzle>>::ok(std::move(storage_engine));
}

Result<void> Dazzle::set_compaction_config(CompactionConfig config) {
    return std::visit(
        [this](auto& opts) {
            using T = std::decay_t<decltype(opts)>;
            if constexpr (std::is_same_v<T, SizeTieredConfig>) {
                compaction_config_ = std::move(opts);
                return Result<void>::ok();
            } else {
                return Result<void>::err(
                    Error{ErrorCode::BAD_CONFIG,
                          "Invalid compaction configuration provided."});
            }
        },
        config);
}

Result<void> Dazzle::put(const storage::Key& key,
                         std::span<const uint8_t> value) {
    if (value.empty()) {
        return Result<void>::err(
            Error{ErrorCode::BAD_CONFIG, "value is empty"});
    }
    return put(key, value, false);
}

Result<void> Dazzle::remove(const storage::Key& key) {
    return put(key, std::nullopt, true);
}

Result<void> Dazzle::put(const storage::Key& key,
                         const std::optional<std::span<const uint8_t>> value,
                         bool remove) {
    WalOpType op = remove ? WalOpType::DELETE_ROW : WalOpType::PUT_ROW;
    auto sequence = bump_lsn_sequence();

    WalRecord record{
        op, hlc_.next(), sequence, key,
        value.has_value()
            ? std::vector<uint8_t>{value.value().begin(), value.value().end()}
            : std::vector<uint8_t>{}};

    /* write and sync WAL */
    auto& writer = wal_writer_.value();
    auto write_result = writer.append(record);
    if (!write_result.has_value()) {
        return Result<void>::err(write_result.error());
    }

    auto sync_result = writer.sync();
    if (!sync_result.has_value()) {
        return Result<void>::err(sync_result.error());
    }

    /* update memtable */
    if (remove) {
        active_memtable_.remove(key, sequence);
    } else {
        active_memtable_.put(key, value.value(), sequence);
    }

    /* check if memtable needs to flush and create new sstable */
    if (active_memtable_.should_flush()) {
        auto flush_result = flush();
        if (!flush_result.has_value()) {
            return Result<void>::err(flush_result.error());
        }
    }

    /* check if we need to compact */
    if (should_compact()) {
        auto cres = do_compact_work();
        // @TODO: This should later move to a background thread right now
        //        compaction sits on a HOT PATH
        if (!cres.has_value()) {
            LOG_ERROR(Category::Compaction, "Failed to compact file={}",
                      cres.error().message);
        }
    }

    return Result<void>::ok();
}

Result<SSTableId> Dazzle::do_compact_work() {
    std::vector<SSTableId> inputs;
    inputs.reserve(sst_readers_.size());
    for (const auto& [id, _] : sst_readers_) {
        inputs.push_back(id);
    }

    std::vector<SSTableId> live;
    live.reserve(sst_readers_.size());
    for (const auto& [id, _] : sst_readers_) {
        live.push_back(id);
    }

    SSTableId sstid;
    Error err{ErrorCode::NONE, ""};

    /* actual compaction */
    std::visit(
        [this, &inputs, &live, &sstid, &err](const auto& opts) {
            using T = std::decay_t<decltype(opts)>;
            if constexpr (std::is_same_v<T, SizeTieredConfig>) {
                bool is_full_compact = can_drop_tombstones(live, inputs);
                auto new_sequence = this->mint_sst_id();
                auto new_sst_result = compactor_.do_size_tiered_compact(
                    inputs, new_sequence, is_full_compact);
                if (!new_sst_result.has_value()) {
                    err = new_sst_result.error();
                    return;
                }

                sstid = new_sst_result.value();
            } else {
                server_panic(
                    "Unidentified or invalid option type for compaction!");
            }
        },
        compaction_config_);

    /* Check if we errored out above */
    if (err.code != ErrorCode::NONE) {
        return Result<SSTableId>::err(err);
    }

    auto sstrr = SSTableReader::create(engine_, sst_path(sstid.value));
    if (!sstrr.has_value()) {
        /* We need to delete this file on failure if the reader wasn't able
         * to open this compaction didn't happen - best effort delete,
         * Manifest and recover should handle if anything goes wrong here */
        engine_.remove(sst_path(sstid.value));
        return Result<SSTableId>::err(sstrr.error());
    }

    /* Remove SSTable Readers that are NO longer needed */
    std::vector<decltype(sst_readers_)::iterator> to_erase;
    for (auto& i : inputs) {
        auto it = sst_readers_.find(i);
        if (it == sst_readers_.end()) {
            return Result<SSTableId>::err(Error::unexpected(
                "FATAL: Compacted input supplied no longer exists!"));
        }
        to_erase.push_back(it);
    }
    for (auto it : to_erase) {
        sst_readers_.erase(it);
    }

    /* Emplace the new reader */
    sst_readers_.emplace(
        sstid, std::make_unique<SSTableReader>(std::move(sstrr.value())));

    /* delete the old files - best effort rn later on obsolete sst files
     * will be cleaned up by a Manifest driven GC
     * @TODO: Need a GC for the deleted file */
    for (const auto& sst_id : inputs) {
        auto path = sst_path(sst_id.value);
        if (fs::exists(path)) {
            if (auto res = engine_.remove(path); !res.has_value()) {
                // @TODO: Handle this
            }
        } else {
            // @TODO: Handle this
        }
    }

    return Result<SSTableId>::ok(sstid);
}

Result<void> Dazzle::flush() {
    if (active_memtable_.count() == 0) {
        return Result<void>::ok();
    }

    auto new_sequence = mint_sst_id();

    /* create a new sstable writer */
    auto sstwrr = SSTableWriter::create(engine_, sst_path(new_sequence),
                                        active_memtable_.count());
    if (!sstwrr.has_value()) {
        return Result<void>::err(sstwrr.error());
    }

    /* itr memtable and add entry to the sstable */
    auto& writer = sstwrr.value();
    for (auto it = active_memtable_.begin(); it != active_memtable_.end();
         it++) {
        auto add_result = writer.add(it->first, it->second);
        if (!add_result.has_value()) {
            return Result<void>::err(add_result.error());
        }
    }

    if (auto finish_result = writer.finish(); !finish_result.has_value()) {
        return Result<void>::err(finish_result.error());
    }

    /* open an sstable reader */
    auto sstrr = SSTableReader::create(engine_, sst_path(new_sequence));
    if (!sstrr.has_value()) {
        return Result<void>::err(sstrr.error());
    }

    /* create new wal sequence */
    auto new_wal_seq = mint_wal_id();
    auto walwrr = WalWriter::create(engine_, wal_path(new_wal_seq));
    if (!walwrr.has_value()) {
        return Result<void>::err(walwrr.error());
    }

    sst_readers_.insert(std::make_pair(
        SSTableId{new_sequence},
        std::make_unique<SSTableReader>(std::move(sstrr.value()))));

    wal_writer_.emplace(std::move(walwrr.value()));

    /* replace with a new empty memtable */
    Memtable mtable{memtable_size_};
    active_memtable_ = std::move(mtable);

    auto old_wal_seq = new_wal_seq - 1;

    /* best effort del, deleting failure for old wals are not fatal */
    fs::remove(wal_path(old_wal_seq));

    return Result<void>::ok();
}

bool Dazzle::should_compact() const {
    return std::visit(
        [this](const auto& opts) -> bool {
            using T = std::decay_t<decltype(opts)>;
            if constexpr (std::is_same_v<T, SizeTieredConfig>) {
                return should_compact_size_tiered(opts);
            } else {
                server_panic(
                    "Unidentified or invalid option type for compaction!");
            }
        },
        compaction_config_);
}

bool Dazzle::should_compact_size_tiered(const SizeTieredConfig& opts) const {
    return sst_readers_.size() >= opts.min_merge_width_;
}

// @FIXME: `Result` is already an optional type need to fix this - this
// results
//          in very weird code checks
Result<std::optional<storage::Value>> Dazzle::get(const storage::Key& key) {
    /* first check memtable */
    auto found = active_memtable_.get(key);
    if (found.has_value()) {
        if (found.value().is_tombstone) {
            return Result<std::optional<storage::Value>>::ok(std::nullopt);
        }
        return Result<std::optional<storage::Value>>::ok(
            storage::Value{std::move(found.value().data)});
    }

    /* reverse lookup on every sstable */
    for (auto it = sst_readers_.rbegin(); it != sst_readers_.rend(); ++it) {
        auto lookup_result = it->second->get(key);
        if (!lookup_result.has_value()) { /* result impl always errors out
                                             if has_value is false */
            return Result<std::optional<storage::Value>>::err(
                lookup_result.error());
        }

        auto found = lookup_result.value();
        if (found.has_value()) {
            if (found.value().is_tombstone) {
                return Result<std::optional<storage::Value>>::ok(std::nullopt);
            }
            return Result<std::optional<storage::Value>>::ok(
                storage::Value{std::move(found.value().data)});
        }
    }

    return Result<std::optional<storage::Value>>::ok(std::nullopt);
}

Result<uint64_t> Dazzle::recover() {
    if (!fs::is_directory(get_wal_directory())) {
        return Result<uint64_t>::err(
            Error{ErrorCode::UNEXPECTED_ERR, "wal directory does not exist"});
    }

    /* collect only OLD wal files (before the current active WAL) */
    std::vector<fs::path> old_wal_files;
    for (const auto& entry : fs::directory_iterator(get_wal_directory())) {
        if (!entry.is_regular_file() || entry.path().extension() != ".log")
            continue;
        auto seq = extract_num(entry.path().filename().string());
        if (seq < peek_wal_id()) {
            old_wal_files.push_back(entry.path());
        }
    }

    if (old_wal_files.empty()) {
        return Result<uint64_t>::ok(0);
    }

    std::sort(old_wal_files.begin(), old_wal_files.end(),
              [](const fs::path& a, const fs::path& b) {
                  return extract_num(a.filename().string()) <
                         extract_num(b.filename().string());
              });

    uint64_t highest_wal_lsn_sequence = 0;

    /* replay old WAL records into the active memtable */
    for (const auto& entry : old_wal_files) {
        auto wal_reader_res = WalReader::create(engine_, entry);
        if (!wal_reader_res.has_value()) {
            return Result<uint64_t>::err(wal_reader_res.error());
        }

        auto& wal_reader = wal_reader_res.value();
        while (true) {
            auto possible_next = wal_reader.next();
            if (!possible_next.has_value()) {
                break;
            }

            auto wal_record = possible_next.value();

            /* check for wal sequence */
            if (highest_wal_lsn_sequence < wal_record.sequence) {
                highest_wal_lsn_sequence = wal_record.sequence;
            }

            if (wal_record.op_type == WalOpType::DELETE_ROW) {
                active_memtable_.remove(wal_record.key, wal_record.sequence);
            } else {
                active_memtable_.put(wal_record.key, wal_record.value,
                                     wal_record.sequence);
            }
        }
    }

    /* flush recovered data into a new sstable */
    if (active_memtable_.approximate_size() > 0) {
        auto flush_result = flush();
        if (!flush_result.has_value()) {
            return Result<uint64_t>::err(flush_result.error());
        }
    }

    /* clean up the old WAL segments that were replayed */
    for (const auto& f : old_wal_files) {
        fs::remove(f);
    }

    /* reconcile */
    lsn_.store(std::max(lsn_.load(), highest_wal_lsn_sequence) + 1);

    return Result<uint64_t>::ok(highest_wal_lsn_sequence);
}

}  // namespace enigmadb::dazzle
