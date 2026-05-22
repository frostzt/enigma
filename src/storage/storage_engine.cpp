#include "enigmadb/storage/storage_engine.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "enigmadb/common/error.h"
#include "enigmadb/storage/key_encoding.h"
#include "enigmadb/storage/memtable/memtable.h"
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
    ss << data_dir_ << "/wal/wal_" << std::setfill('0') << std::setw(8) << seq
       << ".log";
    return ss.str();
}

std::string StorageEngine::sst_path(uint64_t seq) {
    std::stringstream ss;
    ss << data_dir_ << "/sst/sst_" << std::setfill('0') << std::setw(8) << seq
       << ".db";
    return ss.str();
}

Result<StorageEngine> StorageEngine::open(io::IOEngine& engine,
                                          std::string& data_dir,
                                          const uint64_t memtable_size) {
    /* create dirs if they don't exist */
    fs::path wal_dir_path = data_dir + "/wal";
    fs::path sst_dir_path = data_dir + "/sst";
    if (!fs::is_directory(wal_dir_path)) {
        if (!fs::create_directory(wal_dir_path)) {
            return Result<StorageEngine>::err(common::Error{
                common::ErrorCode::UNEXPECTED_ERR, "failed to create wal dir"});
        }
    }

    if (!fs::is_directory(sst_dir_path)) {
        if (!fs::create_directory(sst_dir_path)) {
            return Result<StorageEngine>::err(common::Error{
                common::ErrorCode::UNEXPECTED_ERR, "failed to create sst dir"});
        }
    }

    /* sst files */
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(sst_dir_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".db") {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end(),
              [](const fs::path& a, const fs::path& b) {
                  return extract_num(a.filename().string()) <
                         extract_num(b.filename().string());
              });

    std::vector<sstable::SSTableReader> sst_readers;
    uint64_t highest_sst_seq = 0;
    if (!files.empty()) {
        highest_sst_seq = extract_num(files.back().filename().string());
    }

    for (const auto& entry : files) {
        /* open an sstable reader */
        auto reader = sstable::SSTableReader::create(engine, entry);
        if (reader.has_value()) {
            sst_readers.emplace_back(std::move(reader.value()));
        }
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
        return Result<StorageEngine>::err(wal_writer_res.err());
    }

    StorageEngine storage_engine{engine,
                                 std::move(data_dir),
                                 std::move(wal_writer_res.value()),
                                 memtable_size,
                                 std::move(mtable),
                                 std::move(sst_readers),
                                 highest_wal_seq + 1,
                                 highest_sst_seq + 1};
    storage_engine.recover();

    return Result<StorageEngine>::ok(std::move(storage_engine));
}

Result<void> StorageEngine::put(const std::vector<uint8_t>& partition_key,
                                const std::vector<uint8_t>& clustering_key,
                                const std::string& column_name,
                                const std::vector<uint8_t>& value) {
    if (value.empty()) {
        return Result<void>::err(
            Error{ErrorCode::BAD_CONFIG, "value is empty"});
    }
    return put_record(partition_key, clustering_key, column_name, value, false);
}

Result<void> StorageEngine::remove(const std::vector<uint8_t>& partition_key,
                                   const std::vector<uint8_t>& clustering_key,
                                   const std::string& column_name) {
    return put_record(partition_key, clustering_key, column_name, std::nullopt,
                      true);
}

Result<void> StorageEngine::put_record(
    const std::vector<uint8_t>& partition_key,
    const std::vector<uint8_t>& clustering_key, const std::string& column_name,
    const std::optional<std::vector<uint8_t>>& value, bool remove) {
    WalOpType op = remove ? WalOpType::DELETE_ROW : WalOpType::PUT_ROW;
    std::vector<WalColumn> columns;
    if (!remove) {
        columns.push_back(WalColumn{column_name, value.value()});
    }
    WalRecord record{op,     hlc_.next(), lsn_++, partition_key, clustering_key,
                     columns};

    /* write and sync WAL */
    auto& writer = wal_writer_.value();
    auto write_result = writer.append(record);
    if (!write_result.has_value()) return write_result.err();
    auto sync_result = writer.sync();
    if (!sync_result.has_value()) return sync_result.err();

    /* update memtable */
    if (remove) {
        active_memtable_.remove(partition_key, clustering_key, column_name);
    } else {
        active_memtable_.put(partition_key, clustering_key, column_name,
                             value.value());
    }
    if (active_memtable_.should_flush()) {
        return flush();
    }
    return Result<void>::ok();
}

Result<void> StorageEngine::flush() {
    /* create a new sstable writer */
    auto create_writer =
        SSTableWriter::create(engine_, sst_path(next_sst_seq_));
    if (!create_writer.has_value()) {
        return Result<void>::err(create_writer.err());
    }

    /* itr memtable and add entry to the sstable */
    auto& writer = create_writer.value();
    for (auto it = active_memtable_.begin(); it != active_memtable_.end();
         it++) {
        auto add_result = writer.add(it->first, it->second);
        if (!add_result.has_value()) {
            return Result<void>::err(add_result.err());
        }
    }

    if (auto finish_result = writer.finish(); !finish_result.has_value()) {
        return Result<void>::err(finish_result.err());
    }

    /* open an sstable reader */
    auto create_reader =
        SSTableReader::create(engine_, sst_path(next_sst_seq_));
    if (!create_reader.has_value()) {
        return Result<void>::err(create_reader.err());
    }
    sst_readers_.emplace_back(std::move(create_reader.value()));

    /* replace with a new empty memtable */
    Memtable mtable{memtable_size_};
    active_memtable_ = std::move(mtable);

    auto create_wal_writer =
        WalWriter::create(engine_, wal_path(next_wal_seq_ + 1));
    if (!create_wal_writer.has_value()) {
        return Result<void>::err(create_wal_writer.err());
    }
    wal_writer_.emplace(std::move(create_wal_writer.value()));

    if (!fs::remove(wal_path(next_wal_seq_))) {
        return Result<void>::err(
            Error{ErrorCode::UNEXPECTED_ERR, "File not found!"});
    }

    next_sst_seq_++;
    next_wal_seq_++;
    return Result<void>::ok();
}

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
    for (auto it = sst_readers_.rbegin(); it != sst_readers_.rend(); ++it) {
        auto lookup_result = it->get(
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

Result<void> StorageEngine::recover() {
    fs::path wal_dir_path = data_dir_ + "/wal";
    if (!fs::is_directory(wal_dir_path)) {
        return Result<void>::err(
            Error{ErrorCode::UNEXPECTED_ERR, "wal directory does not exist"});
    }

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

    /* recover data reading the wal files */
    uint64_t highest_wal_seq = 0;
    for (const auto& entry : wal_log_files) {
        auto filename = entry.filename();
        auto seq_found = extract_num(filename);
        if (seq_found > highest_wal_seq) {
            highest_wal_seq = seq_found;
        }

        auto wal_reader_res = wal::WalReader::create(engine_, entry);
        if (!wal_reader_res.has_value()) {
            return Result<void>::err(wal_reader_res.err());
        }

        auto& wal_reader = wal_reader_res.value();
        while (true) {
            auto possible_next = wal_reader.next();
            if (!possible_next.has_value()) {
                break;
            }

            auto wal_record = possible_next.value();
            if (wal_record.op_type == WalOpType::DELETE_COLUMN ||
                wal_record.op_type == WalOpType::DELETE_ROW ||
                wal_record.op_type == WalOpType::DELETE_PARTITION) {
                for (const auto& col : wal_record.columns) {
                    active_memtable_.remove(wal_record.partition_key,
                                            wal_record.clustering_key,
                                            col.name);
                }
            } else {
                for (const auto& col : wal_record.columns) {
                    active_memtable_.put(wal_record.partition_key,
                                         wal_record.clustering_key, col.name,
                                         col.value);
                }
            }
        }
    }

    /* flush into a new sstable */
    flush();

    auto deleted_num = fs::remove_all(wal_dir_path);
    assert(deleted_num == wal_log_files.size());

    return Result<void>::ok();
}

}  // namespace enigmadb::storage
