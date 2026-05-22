#ifndef ENIGMA_DB_STORAGE_ENGINE_H
#define ENIGMA_DB_STORAGE_ENGINE_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "enigmadb/common/hlc.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/memtable/memtable.h"
#include "enigmadb/storage/sstable/sstable_reader.h"
#include "enigmadb/storage/wal/wal_writer.h"

namespace enigmadb::storage {

template <typename T>
using Result = common::ExpectResult<T, common::Error>;

class StorageEngine {
   private:
    io::IOEngine& engine_;
    const std::string data_dir_;

    std::optional<wal::WalWriter> wal_writer_;
    uint64_t memtable_size_;
    memtable::Memtable active_memtable_;
    std::vector<sstable::SSTableReader> sst_readers_;
    common::TimestampGenerator hlc_;
    uint64_t lsn_;
    uint64_t next_wal_seq_;
    uint64_t next_sst_seq_;

    StorageEngine(io::IOEngine& engine, std::string data_dir,
                  wal::WalWriter wal_writer, uint64_t memtable_size,
                  memtable::Memtable active_memtable,
                  std::vector<sstable::SSTableReader> sst_readers,
                  uint64_t next_wal_seq, uint64_t next_sst_seq)
        : engine_(engine),
          data_dir_(std::move(data_dir)),
          wal_writer_(std::move(wal_writer)),
          memtable_size_(memtable_size),
          active_memtable_(std::move(active_memtable)),
          sst_readers_(std::move(sst_readers)),
          lsn_(0),
          next_wal_seq_(next_wal_seq),
          next_sst_seq_(next_sst_seq) {}

    std::string wal_path(uint64_t seq);
    std::string sst_path(uint64_t seq);

    Result<void> put_record(const std::vector<uint8_t>& partition_key,
                            const std::vector<uint8_t>& clustering_key,
                            const std::string& column_name,
                            const std::optional<std::vector<uint8_t>>& value,
                            bool remove);

    Result<void> recover();

   public:
    static Result<StorageEngine> open(io::IOEngine& engine,
                                      std::string& data_dir,
                                      const uint64_t memtable_size);

    Result<void> put(const std::vector<uint8_t>& partition_key,
                     const std::vector<uint8_t>& clustering_key,
                     const std::string& column_name,
                     const std::vector<uint8_t>& value);

    Result<void> remove(const std::vector<uint8_t>& partition_key,
                        const std::vector<uint8_t>& clustering_key,
                        const std::string& column_name);

    Result<std::optional<memtable::MemtableValue>> get(
        const std::vector<uint8_t>& partition_key,
        const std::vector<uint8_t>& clustering_key,
        const std::string& column_name);

    // Flush the current memtable to an SSTable
    Result<void> flush();
};

}  // namespace enigmadb::storage

#endif  // ENIGMA_DB_STORAGE_ENGINE_H
