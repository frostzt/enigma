#include "enigmadb/storage/storage_engine.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "enigmadb/common/error.h"
#include "enigmadb/storage/memtable/memtable.h"
#include "enigmadb/storage/sstable/sstable_reader.h"
#include "enigmadb/storage/wal/wal_reader.h"
#include "enigmadb/storage/wal/wal_record.h"
#include "enigmadb/storage/wal/wal_writer.h"

namespace fs = std::filesystem;

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

    memtable::Memtable mtable{memtable_size};
    uint64_t highest_wal_seq = 0;
    for (const auto& entry : wal_log_files) {
        /* find the highest sequence */
        auto filename = entry.filename();
        auto seq_found = extract_num(filename);
        if (seq_found > highest_wal_seq) {
            highest_wal_seq = seq_found;
        }

        auto wal_reader_res = wal::WalReader::create(engine, entry);
        if (!wal_reader_res.has_value()) {
            return Result<StorageEngine>::err(wal_reader_res.err());
        }

        auto& wal_reader = wal_reader_res.value();
        while (true) {
            auto possible_next = wal_reader.next();
            if (!possible_next.has_value()) {
                break;
            }

            auto wal_record = possible_next.value();

            if (wal_record.op_type == wal::WalOpType::DELETE_COLUMN ||
                wal_record.op_type == wal::WalOpType::DELETE_ROW ||
                wal_record.op_type == wal::WalOpType::DELETE_PARTITION) {
                for (const auto& col : wal_record.columns) {
                    mtable.remove(wal_record.partition_key,
                                  wal_record.clustering_key, col.name);
                }
            } else {
                for (const auto& col : wal_record.columns) {
                    mtable.put(wal_record.partition_key,
                               wal_record.clustering_key, col.name, col.value);
                }
            }
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
                                 std::move(mtable),
                                 std::move(sst_readers),
                                 highest_wal_seq + 1,
                                 highest_sst_seq + 1};

    return Result<StorageEngine>::ok(std::move(storage_engine));
}

}  // namespace enigmadb::storage
