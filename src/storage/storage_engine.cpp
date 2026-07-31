#include "enigmadb/storage/storage_engine.h"

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

#include "enigmadb/common/error.h"
#include "enigmadb/common/utils.h"
#include "enigmadb/storage/compaction/compaction.h"
#include "enigmadb/storage/key_encoding.h"
#include "enigmadb/storage/memtable/memtable.h"
#include "enigmadb/storage/sstable/sstable_common.h"
#include "enigmadb/storage/sstable/sstable_reader.h"
#include "enigmadb/storage/sstable/sstable_writer.h"
#include "enigmadb/storage/wal/wal_reader.h"
#include "enigmadb/storage/wal/wal_record.h"
#include "enigmadb/storage/wal/wal_writer.h"

namespace fs = std::filesystem;

using namespace enigmadb::common;
using namespace enigmadb::storage::wal;
using namespace enigmadb::storage::memtable;
using namespace enigmadb::storage::sstable;

namespace enigmadb::storage {

/* TODO: This is quite error prone but given we control file name should be good
 * for now */
uint64_t extract_num(const std::string& filename) {
    size_t start = filename.find("_") + 1;
    size_t end = filename.find(".");
    return std::stoll(filename.substr(start, end - start));
}

std::string StorageEngine::wal_path(uint64_t seq) {
    std::stringstream ss;
    ss << get_wal_directory() << "/wal_" << std::setfill('0') << std::setw(8)
       << seq << ".log";
    return ss.str();
}

std::string StorageEngine::sst_path(uint64_t seq) {
    std::stringstream ss;
    ss << get_sst_directory() << "/" << sstable_filename(SSTableId{seq});
    return ss.str();
}

Result<std::unique_ptr<StorageEngine>> StorageEngine::open(
    io::IOEngine& engine, const std::string& data_dir,
    const uint64_t memtable_size) {
    if (trim_string(data_dir) == "") {
        return Result<std::unique_ptr<StorageEngine>>::err(
            Error{ErrorCode::BAD_CONFIG, "Data directory was not specified."});
    }

    /* create dirs if they don't exist */
    fs::path wal_dir_path = data_dir + "/wal";
    fs::path sst_dir_path = data_dir + "/sst";
    if (!fs::is_directory(wal_dir_path)) {
        if (!fs::create_directory(wal_dir_path)) {
            return Result<std::unique_ptr<StorageEngine>>::err(common::Error{
                common::ErrorCode::UNEXPECTED_ERR, "failed to create wal dir"});
        }
    }

    if (!fs::is_directory(sst_dir_path)) {
        if (!fs::create_directory(sst_dir_path)) {
            return Result<std::unique_ptr<StorageEngine>>::err(common::Error{
                common::ErrorCode::UNEXPECTED_ERR, "failed to create sst dir"});
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

    std::map<sstable::SSTableId, std::unique_ptr<sstable::SSTableReader>,
             SSTableIdComparator>
        sst_readers;

    uint64_t max_sst_sequence_found = 0;

    for (const auto& entry : files) {
        /* open an sstable reader */
        auto sstr = sstable::SSTableReader::create(engine, entry);
        if (!sstr.has_value()) return sstr.err();
        auto& reader = sstr.value();
        auto sstfooter = reader.get_footer();
        assert(sstfooter.has_value());

        auto& footer = sstfooter.value();
        if (footer.highest_sequence > max_sst_sequence_found) {
            max_sst_sequence_found = footer.highest_sequence;
        }

        sst_readers.insert(std::make_pair(
            SSTableId{extract_num(entry.filename().string())},
            std::make_unique<sstable::SSTableReader>(std::move(reader))));
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
    auto wal_writer_res = wal::WalWriter::create(engine, ss.str());
    if (!wal_writer_res.has_value()) {
        return Result<std::unique_ptr<StorageEngine>>::err(
            wal_writer_res.err());
    }

    auto storage_engine = std::unique_ptr<StorageEngine>(new StorageEngine(
        engine, data_dir, std::move(wal_writer_res.value()), memtable_size,
        std::move(mtable), std::move(sst_readers), highest_wal_seq + 1,
        highest_sst_seq + 1, max_sst_sequence_found + 1));

    /* try and recover */
    auto recover_result = storage_engine->recover();
    if (!recover_result.has_value()) {
        return Result<std::unique_ptr<StorageEngine>>::err(
            recover_result.err());
    }

    return Result<std::unique_ptr<StorageEngine>>::ok(
        std::move(storage_engine));
}

Result<void> StorageEngine::set_compaction_config(
    compaction::CompactionConfig config) {
    return std::visit(
        [this](auto& opts) {
            using T = std::decay_t<decltype(opts)>;
            if constexpr (std::is_same_v<T, compaction::SizeTieredConfig>) {
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

Result<void> StorageEngine::put(const std::vector<uint8_t>& partition_key,
                                const std::vector<uint8_t>& clustering_key,
                                const std::string& column_name,
                                const std::vector<uint8_t>& value) {
    if (value.empty()) {
        return Result<void>::err(
            Error{ErrorCode::BAD_CONFIG, "value is empty"});
    }
    return put(partition_key, clustering_key, column_name, value, false);
}

Result<void> StorageEngine::remove(const std::vector<uint8_t>& partition_key,
                                   const std::vector<uint8_t>& clustering_key,
                                   const std::string& column_name) {
    return put(partition_key, clustering_key, column_name, std::nullopt, true);
}

Result<void> StorageEngine::put(
    const std::vector<uint8_t>& partition_key,
    const std::vector<uint8_t>& clustering_key, const std::string& column_name,
    const std::optional<std::vector<uint8_t>>& value, bool remove) {
    WalOpType op = remove ? WalOpType::DELETE_ROW : WalOpType::PUT_ROW;
    std::vector<WalColumn> columns;
    if (!remove) {
        columns.push_back(WalColumn{column_name, value.value()});
    }
    auto sequence = bump_lsn_sequence();
    WalRecord record{
        op, hlc_.next(), sequence, partition_key, clustering_key, columns};

    /* write and sync WAL */
    auto& writer = wal_writer_.value();
    auto write_result = writer.append(record);
    if (!write_result.has_value()) return write_result.err();
    auto sync_result = writer.sync();
    if (!sync_result.has_value()) return sync_result.err();

    /* update memtable */
    if (remove) {
        active_memtable_.remove(partition_key, clustering_key, column_name,
                                sequence);
    } else {
        active_memtable_.put(partition_key, clustering_key, column_name,
                             value.value(), sequence);
    }

    /* check if memtable needs to flush and create new sstable */
    if (active_memtable_.should_flush()) {
        auto flush_result = flush();
        if (!flush_result.has_value()) {
            return flush_result.err();
        }
    }

    /* check if we need to compact */
    if (should_compact()) {
        auto cres = do_compact_work();
        // @TODO: This should later move to a background thread right now
        //        compaction sits on a HOT PATH
        if (!cres.has_value()) {
            std::cerr << "[COMPACTION] Failed to compact file: "
                      << cres.err().message << std::endl;
        }
    }

    return Result<void>::ok();
}

Result<SSTableId> StorageEngine::do_compact_work() {
    std::vector<sstable::SSTableId> inputs;
    inputs.reserve(sst_readers_.size());
    for (const auto& [id, _] : sst_readers_) {
        inputs.push_back(id);
    }

    SSTableId sstid;
    Error err{ErrorCode::NONE, ""};

    /* actual compaction */
    std::visit(
        [this, &inputs, &sstid, &err](const auto& opts) {
            using T = std::decay_t<decltype(opts)>;
            if constexpr (std::is_same_v<T, compaction::SizeTieredConfig>) {
                bool is_full_compact = inputs.size() == sst_readers_.size();
                auto new_sst_result = compactor_.do_size_tiered_compact(
                    inputs, this->get_next_sst_sequence(), is_full_compact);
                if (!new_sst_result.has_value()) {
                    err = new_sst_result.err();
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

    assert(sstid.value == get_next_sst_sequence());

    auto sstrr = SSTableReader::create(engine_, sst_path(sstid.value));
    if (!sstrr.has_value()) {
        /* We need to delete this file on failure if the reader wasn't able to
         * open this compaction didn't happen - best effort delete, Manifest
         * and recover should handle if anything goes wrong here */
        engine_.remove(sst_path(sstid.value));
        return Result<SSTableId>::err(sstrr.err());
    }

    /* Empty the sst_readers_ vector and update it to use the new
     * sstable file */
    sst_readers_.clear();
    sst_readers_.emplace(sstid, std::make_unique<sstable::SSTableReader>(
                                    std::move(sstrr.value())));

    /* Bump sstable file sequence the newly generated one should have had the
     * last one */
    bump_sst_sequence();

    /* delete the old files - best effort rn later on obsolete sst files will be
     * cleaned up by a Manifest driven GC
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

Result<void> StorageEngine::flush() {
    if (active_memtable_.count() == 0) {
        return Result<void>::ok();
    }

    /* create a new sstable writer */
    auto sstwrr = SSTableWriter::create(
        engine_, sst_path(get_next_sst_sequence()), active_memtable_.count());
    if (!sstwrr.has_value()) return sstwrr.err();

    /* itr memtable and add entry to the sstable */
    auto& writer = sstwrr.value();
    for (auto it = active_memtable_.begin(); it != active_memtable_.end();
         it++) {
        auto add_result = writer.add(it->first, it->second);
        if (!add_result.has_value()) return add_result.err();
    }

    if (auto finish_result = writer.finish(); !finish_result.has_value()) {
        return Result<void>::err(finish_result.err());
    }

    /* open an sstable reader */
    auto sstrr =
        SSTableReader::create(engine_, sst_path(get_next_sst_sequence()));
    if (!sstrr.has_value()) return sstrr.err();

    /* create new wal sequence */
    auto new_wal_seq = get_next_wal_sequence() + 1;
    auto walwrr = WalWriter::create(engine_, wal_path(new_wal_seq));
    if (!walwrr.has_value()) return walwrr.err();

    sst_readers_.insert(std::make_pair(
        SSTableId{get_next_sst_sequence()},
        std::make_unique<sstable::SSTableReader>(std::move(sstrr.value()))));

    wal_writer_.emplace(std::move(walwrr.value()));

    /* replace with a new empty memtable */
    Memtable mtable{memtable_size_};
    active_memtable_ = std::move(mtable);

    auto old_wal_seq = get_next_wal_sequence();
    bump_wal_sequence();
    bump_sst_sequence();

    /* best effort del, deleting failure for old wals are not fatal */
    fs::remove(wal_path(old_wal_seq));

    return Result<void>::ok();
}

bool StorageEngine::should_compact() const {
    return std::visit(
        [this](const auto& opts) -> bool {
            using T = std::decay_t<decltype(opts)>;
            if constexpr (std::is_same_v<T, compaction::SizeTieredConfig>) {
                return should_compact_size_tiered(opts);
            } else {
                server_panic(
                    "Unidentified or invalid option type for compaction!");
            }
        },
        compaction_config_);
}

bool StorageEngine::should_compact_size_tiered(
    const compaction::SizeTieredConfig& opts) const {
    return sst_readers_.size() >= opts.min_merge_width_;
}

// @FIXME: `Result` is already an optional type need to fix this - this results
//          in very weird code checks
Result<std::optional<MemtableValue>> StorageEngine::get(
    const std::vector<uint8_t>& partition_key,
    const std::vector<uint8_t>& clustering_key,
    const std::string& column_name) {
    /* first check memtable */
    auto found =
        active_memtable_.get(partition_key, clustering_key, column_name);
    if (found.has_value()) {
        if (found.value().is_tombstone) {
            return Result<std::optional<MemtableValue>>::ok(std::nullopt);
        }
        return Result<std::optional<MemtableValue>>::ok(found);
    }

    /* reverse lookup on every sstable */
    for (auto it = sst_readers_.begin(); it != sst_readers_.end(); ++it) {
        auto lookup_result = it->second->get(
            encode_composite_key(partition_key, clustering_key, column_name));
        if (!lookup_result.has_value()) { /* result impl always errors out if
                                             has_value is false */
            return Result<std::optional<MemtableValue>>::err(
                lookup_result.err());
        }

        auto found = lookup_result.value();
        if (found.has_value()) {
            if (found.value().is_tombstone) {
                return Result<std::optional<MemtableValue>>::ok(std::nullopt);
            }
            return Result<std::optional<MemtableValue>>::ok(found);
        }
    }

    return Result<std::optional<MemtableValue>>::ok(std::nullopt);
}

Result<uint64_t> StorageEngine::recover() {
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
        if (seq < get_next_wal_sequence()) {
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
        auto wal_reader_res = wal::WalReader::create(engine_, entry);
        if (!wal_reader_res.has_value()) {
            return Result<uint64_t>::err(wal_reader_res.err());
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

            if (wal_record.op_type == WalOpType::DELETE_COLUMN ||
                wal_record.op_type == WalOpType::DELETE_ROW ||
                wal_record.op_type == WalOpType::DELETE_PARTITION) {
                for (const auto& col : wal_record.columns) {
                    active_memtable_.remove(wal_record.partition_key,
                                            wal_record.clustering_key, col.name,
                                            wal_record.sequence);
                }
            } else {
                for (const auto& col : wal_record.columns) {
                    active_memtable_.put(wal_record.partition_key,
                                         wal_record.clustering_key, col.name,
                                         col.value, wal_record.sequence);
                }
            }
        }
    }

    /* flush recovered data into a new sstable */
    if (active_memtable_.approximate_size() > 0) {
        auto flush_result = flush();
        if (!flush_result.has_value()) {
            return Result<uint64_t>::err(flush_result.err());
        }
    }

    /* clean up the old WAL segments that were replayed */
    for (const auto& f : old_wal_files) {
        fs::remove(f);
    }

    /* reconcile */
    auto current = lsn_.load(std::memory_order_relaxed);
    auto target = std::max(current, highest_wal_lsn_sequence) + 1;
    while (current < target &&
           !lsn_.compare_exchange_weak(current, target,
                                       std::memory_order_relaxed)) {
        target = std::max(current, highest_wal_lsn_sequence) + 1;
    }

    return Result<uint64_t>::ok(highest_wal_lsn_sequence);
}

}  // namespace enigmadb::storage
