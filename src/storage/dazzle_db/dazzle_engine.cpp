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
#include <vector>

#include "enigmadb/base.h"
#include "enigmadb/error.h"
#include "enigmadb/storage/dazzle_db/compaction/compaction.h"
#include "enigmadb/storage/dazzle_db/compaction/compaction_policy.h"
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
    ss << get_wal_directory() << "/wal_" << std::setfill('0') << std::setw(8) << seq << ".log";
    return ss.str();
}

std::string Dazzle::sst_path(uint64_t seq) {
    std::stringstream ss;
    ss << get_sst_directory() << "/" << sstable_filename(SSTableId{seq});
    return ss.str();
}

Result<std::optional<InternalValue>> Version::lookup_internal(const storage::Key& key) const {
    for (auto it = sst_readers.rbegin(); it != sst_readers.rend(); ++it) {
        auto lookup_result = it->second->get(key);
        if (!lookup_result.has_value()) {
            return Result<std::optional<InternalValue>>::err(lookup_result.error());
        }

        /* TODO: Optimization here once we add more in SST Metadata to check if this key exists here */
        // auto id = it->first;
        // if (sst_meta.count(id)) {}

        auto found = lookup_result.value();
        if (found.has_value()) {
            if (found.value().is_tombstone) {
                return Result<std::optional<InternalValue>>::ok(std::nullopt);
            }
            return Result<std::optional<InternalValue>>::ok(std::move(found.value()));
        }
    }

    return Result<std::optional<InternalValue>>::ok(std::nullopt);
}

Result<std::optional<storage::Value>> Version::lookup(const storage::Key& key) const {
    for (auto it = sst_readers.rbegin(); it != sst_readers.rend(); ++it) {
        auto lookup_result = it->second->get(key);
        if (!lookup_result.has_value()) {
            return Result<std::optional<storage::Value>>::err(lookup_result.error());
        }

        /* TODO: Optimization here once we add more in SST Metadata to check if this key exists here */
        // auto id = it->first;
        // if (sst_meta.count(id)) {}

        auto found = lookup_result.value();
        if (found.has_value()) {
            if (found.value().is_tombstone) {
                return Result<std::optional<storage::Value>>::ok(std::nullopt);
            }
            return Result<std::optional<storage::Value>>::ok(storage::Value{std::move(found.value().data)});
        }
    }

    return Result<std::optional<storage::Value>>::ok(std::nullopt);
}

Result<std::unique_ptr<Dazzle>> Dazzle::open(io::IOEngine& engine, const std::string& data_dir,
                                             const uint64_t memtable_size, std::unique_ptr<CompactionPolicy> policy) {
    if (trim_string(data_dir) == "") {
        return Result<std::unique_ptr<Dazzle>>::err(Error{ErrorCode::BAD_CONFIG, "Data directory was not specified."});
    }

    /* create dirs if they don't exist */
    fs::path wal_dir_path = data_dir + "/wal";
    fs::path sst_dir_path = data_dir + "/sst";
    if (!fs::is_directory(wal_dir_path)) {
        if (!fs::create_directory(wal_dir_path)) {
            return Result<std::unique_ptr<Dazzle>>::err(Error{ErrorCode::UNEXPECTED_ERR, "failed to create wal dir"});
        }
    }

    if (!fs::is_directory(sst_dir_path)) {
        if (!fs::create_directory(sst_dir_path)) {
            return Result<std::unique_ptr<Dazzle>>::err(Error{ErrorCode::UNEXPECTED_ERR, "failed to create sst dir"});
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

    std::map<SSTableId, std::shared_ptr<SSTableReader>, SSTableIdComparator> sst_readers;
    std::map<SSTableId, std::shared_ptr<SSTableMeta>, SSTableIdComparator> sst_meta;

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

        auto sstid = SSTableId{extract_num(entry.filename().string())};

        sst_readers.insert(std::make_pair(sstid, std::make_unique<SSTableReader>(std::move(reader))));
        sst_meta.insert(
            std::make_pair(sstid, std::make_unique<SSTableMeta>(SSTableMeta{
                                      sstid, footer.size_bytes, footer.entry_count, footer.highest_sequence})));
    }

    /* if wal files exist recover */
    std::vector<fs::path> wal_log_files;
    for (const auto& entry : fs::directory_iterator(wal_dir_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".log") {
            wal_log_files.push_back(entry.path());
        }
    }

    std::sort(wal_log_files.begin(), wal_log_files.end(), [](const fs::path& a, const fs::path& b) {
        return extract_num(a.filename().string()) < extract_num(b.filename().string());
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
    ss << data_dir << "/wal/wal_" << std::setfill('0') << std::setw(8) << highest_wal_seq + 1 << ".log";
    auto wal_writer_res = WalWriter::create(engine, ss.str());
    if (!wal_writer_res.has_value()) {
        return Result<std::unique_ptr<Dazzle>>::err(wal_writer_res.error());
    }

    /* FIXME: Need to revisit this later right now its really bad here with the
     * sequences */
    auto storage_engine = std::unique_ptr<Dazzle>(new Dazzle(
        engine, data_dir, std::move(wal_writer_res.value()), memtable_size, std::move(mtable), std::move(sst_readers),
        std::move(sst_meta), highest_wal_seq + 1, highest_sst_seq + 1, max_sst_sequence_found + 1, std::move(policy)));

    /* try and recover */
    auto recover_result = storage_engine->recover();
    if (!recover_result.has_value()) {
        return Result<std::unique_ptr<Dazzle>>::err(recover_result.error());
    }

    return Result<std::unique_ptr<Dazzle>>::ok(std::move(storage_engine));
}

Result<void> Dazzle::set_compaction_policy(std::unique_ptr<CompactionPolicy> policy) {
    if (!policy) {
        return Result<void>::err(Error::bad_config("Compaction policy was not specified"));
    }

    policy_ = std::move(policy);
    return Result<void>::ok();
}

Result<void> Dazzle::put(const storage::Key& key, std::span<const uint8_t> value) {
    if (value.empty()) {
        return Result<void>::err(Error{ErrorCode::BAD_CONFIG, "value is empty"});
    }
    return put(key, value, false);
}

Result<void> Dazzle::remove(const storage::Key& key) { return put(key, std::nullopt, true); }

Result<void> Dazzle::put(const storage::Key& key, const std::optional<std::span<const uint8_t>> value, bool remove) {
    WalOpType op = remove ? WalOpType::DELETE_ROW : WalOpType::PUT_ROW;
    auto sequence = bump_lsn_sequence();

    WalRecord record{
        op, hlc_.next(), sequence, key,
        value.has_value() ? std::vector<uint8_t>{value.value().begin(), value.value().end()} : std::vector<uint8_t>{}};

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

    return Result<void>::ok();
}

std::vector<const SSTableMeta*> Dazzle::sst_meta_to_vector() const {
    std::vector<const SSTableMeta*> metas;
    auto current = version_set_->get_current();
    for (const auto& [id, meta] : current->sst_meta) {
        metas.push_back(meta.get());
    }
    return metas;
}

Result<std::optional<SSTableId>> Dazzle::do_compact_work() {
    auto c_can = policy_->pick(sst_meta_to_vector());
    if (!c_can.has_value()) {
        return Result<std::optional<SSTableId>>::ok(std::nullopt);
    }
    const auto& candidate = c_can.value();
    return run_task(CompactionTask{candidate.inputs, SSTableId{mint_sst_id()}, candidate.can_drop_tombstone});
}

Result<std::optional<SSTableId>> Dazzle::compact_now() {
    auto metas = sst_meta_to_vector();
    if (metas.empty()) {
        return Result<std::optional<SSTableId>>::ok(std::nullopt);
    }

    std::vector<SSTableId> inputs;
    inputs.reserve(metas.size());
    for (const auto* m : metas) inputs.push_back(m->id);
    std::sort(inputs.begin(), inputs.end());

    return run_task(CompactionTask{inputs, SSTableId{mint_sst_id()},
                                   /* full compaction: inputs == live */ true});
}

Result<std::optional<SSTableId>> Dazzle::run_task(const CompactionTask& task) {
    auto exec_result = execute(task);
    if (!exec_result.has_value()) {
        return Result<std::optional<SSTableId>>::err(exec_result.error());
    }

    /* emplace the new sst reader */
    if (auto r = install(task); !r.has_value()) {
        return Result<std::optional<SSTableId>>::err(r.error());
    }

    return Result<std::optional<SSTableId>>::ok(task.output_id);
}

Result<SSTableId> Dazzle::execute(const CompactionTask& task) {
    return compactor_.compact(task.inputs, task.output_id.value, task.can_drop_tombstone);
}

Result<void> Dazzle::install_flushed_sst(SSTableId new_id, std::shared_ptr<SSTableReader> reader,
                                         std::shared_ptr<SSTableMeta> meta) {
    auto current = version_set_->get_current();
    auto next_version = std::make_shared<Version>();
    next_version->sst_readers = current->sst_readers;
    next_version->sst_meta = current->sst_meta;

    /* insert the new versions */
    next_version->sst_readers[new_id] = std::move(reader);
    next_version->sst_meta[new_id] = std::move(meta);

    version_set_->append_version(next_version);

    return Result<void>::ok();
}

Result<void> Dazzle::install(const CompactionTask& task) {
    auto current = version_set_->get_current();
    auto next_version = std::make_shared<Version>();

    next_version->sst_readers = current->sst_readers;
    next_version->sst_meta = current->sst_meta;

    /* Aggregate and make sure that inputs haven't changed */
    std::vector<decltype(next_version->sst_readers)::iterator> to_erase;
    for (auto& i : task.inputs) {
        auto it = next_version->sst_readers.find(i);
        if (it == next_version->sst_readers.end()) {
            return Result<void>::err(Error::unexpected("FATAL: Compacted input supplied no longer exists!"));
        }
        to_erase.push_back(it);
    }

    /* Create a new SSTable Reader for this new sstable file */
    auto sstrr = SSTableReader::create(engine_, sst_path(task.output_id.value));
    if (!sstrr.has_value()) {
        /* We need to delete this file on failure if the reader wasn't able
         * to open this compaction didn't happen - best effort delete,
         * Manifest and recover should handle if anything goes wrong here */
        engine_.remove(sst_path(task.output_id.value));
        return Result<void>::err(sstrr.error());
    }

    /* Remove the sst readers that are no longer needed */
    std::vector<std::string> removed_paths;
    removed_paths.reserve(to_erase.size());
    for (auto it : to_erase) {
        removed_paths.push_back(sst_path(it->first.value));
        next_version->sst_meta.erase(it->first);
        next_version->sst_readers.erase(it);
    }

    /* add the newly generated files */
    auto& reader = sstrr.value();
    auto footer = reader.get_footer().value();
    next_version->sst_readers[task.output_id] = std::make_shared<SSTableReader>(std::move(reader));
    next_version->sst_meta[task.output_id] =
        std::make_shared<SSTableMeta>(task.output_id, footer.size_bytes, footer.entry_count, footer.highest_sequence);

    /* update version */
    version_set_->append_version(next_version, removed_paths);

    return Result<void>::ok();
}

Result<void> Dazzle::flush() {
    if (active_memtable_.count() == 0) {
        return Result<void>::ok();
    }

    auto new_sequence = mint_sst_id();

    /* create a new sstable writer */
    auto sstwrr = SSTableWriter::create(engine_, sst_path(new_sequence), active_memtable_.count());
    if (!sstwrr.has_value()) {
        return Result<void>::err(sstwrr.error());
    }

    /* itr memtable and add entry to the sstable */
    auto& writer = sstwrr.value();
    for (auto it = active_memtable_.begin(); it != active_memtable_.end(); it++) {
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

    auto& reader = sstrr.value();
    auto footer = reader.get_footer().value();

    auto current = version_set_->get_current();
    auto next_version = std::make_shared<Version>();

    next_version->sst_readers = current->sst_readers;
    next_version->sst_meta = current->sst_meta;

    auto new_id = SSTableId{new_sequence};
    next_version->sst_readers[new_id] = std::make_shared<SSTableReader>(std::move(reader));
    next_version->sst_meta[new_id] =
        std::make_shared<SSTableMeta>(new_id, footer.size_bytes, footer.entry_count, footer.highest_sequence);

    version_set_->append_version(next_version);
    wal_writer_.emplace(std::move(walwrr.value()));

    /* replace with a new empty memtable */
    Memtable mtable{memtable_size_};
    active_memtable_ = std::move(mtable);

    auto old_wal_seq = new_wal_seq - 1;

    /* best effort del, deleting failure for old wals are not fatal */
    fs::remove(wal_path(old_wal_seq));

    /* check if we need to compact */
    auto cres = do_compact_work();
    // @TODO: This should later move to a background thread right now compaction sits on a HOT PATH
    if (!cres.has_value()) {
        std::cerr << "[COMPACTION] Failed to compact file: " << cres.error().message << std::endl;
    }

    return Result<void>::ok();
}

Result<std::optional<storage::Value>> Dazzle::get(const storage::Key& key) {
    auto found = active_memtable_.get(key);
    if (found.has_value()) {
        if (found.value().is_tombstone) {
            return Result<std::optional<storage::Value>>::ok(std::nullopt);
        }
        return Result<std::optional<storage::Value>>::ok(storage::Value{std::move(found.value().data)});
    }

    auto current_disk = version_set_->get_current();
    return current_disk->lookup(key);
}

/* this returns raw key so if tombstone will still return the record wrote this for tests */
Result<std::optional<InternalValue>> Dazzle::get_internal(const storage::Key& key) {
    if (auto found = active_memtable_.get(key); found.has_value()) {
        return Result<std::optional<InternalValue>>::ok(found);
    }

    auto current_disk = version_set_->get_current();
    return current_disk->lookup_internal(key);
}

Result<uint64_t> Dazzle::recover() {
    if (!fs::is_directory(get_wal_directory())) {
        return Result<uint64_t>::err(Error{ErrorCode::UNEXPECTED_ERR, "wal directory does not exist"});
    }

    /* collect only OLD wal files (before the current active WAL) */
    std::vector<fs::path> old_wal_files;
    for (const auto& entry : fs::directory_iterator(get_wal_directory())) {
        if (!entry.is_regular_file() || entry.path().extension() != ".log") continue;
        auto seq = extract_num(entry.path().filename().string());
        if (seq < peek_wal_id()) {
            old_wal_files.push_back(entry.path());
        }
    }

    if (old_wal_files.empty()) {
        return Result<uint64_t>::ok(0);
    }

    std::sort(old_wal_files.begin(), old_wal_files.end(), [](const fs::path& a, const fs::path& b) {
        return extract_num(a.filename().string()) < extract_num(b.filename().string());
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
                active_memtable_.put(wal_record.key, wal_record.value, wal_record.sequence);
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
