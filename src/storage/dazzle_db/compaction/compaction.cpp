#include "enigmadb/storage/dazzle_db/compaction/compaction.h"

#include <memory>
#include <vector>

#include "enigmadb/base.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/log.h"
#include "enigmadb/storage/dazzle_db/core/version.h"
#include "enigmadb/storage/dazzle_db/merge_iterator.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_common.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_reader.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_writer.h"

namespace enigmadb::dazzle {

Compactor Compactor::create(io::IOEngine& engine, const std::string& data_dir) { return Compactor{engine, data_dir}; }

Result<SSTableId> Compactor::compact(std::shared_ptr<Version> snapshot, const std::vector<SSTableId>& inputs,
                                     const uint64_t next_sst_seq, bool is_full_compaction) {
    uint64_t possible_keys = 0;
    std::vector<std::shared_ptr<SSTableReader>> readers;

    LOG_DEBUG(Category::Compaction,
              "Compaction started for {} inputs next sst seq={} should drop "
              "tombstones?={}",
              inputs.size(), next_sst_seq, is_full_compaction);

    /* open readers for all the sstable id inputs */
    for (const auto i : inputs) {
        auto it = snapshot->sst_readers.find(i);
        if (it == snapshot->sst_readers.end()) {
            return Result<SSTableId>::err(
                Error::unexpected("SST file went missing from snapshot before compaction could read it"));
        }

        /* get an estimate amount for next possible keys */
        auto& reader = it->second;
        auto f = reader->get_footer();
        if (!f.has_value()) {
            LOG_ERROR(Category::Compaction, "Failed to fetch footer from a reader for path={} got err={}",
                      sst_path(data_dir_, i.value), f.error().message);
            return Result<SSTableId>::err(f.error());
        }

        possible_keys += f.value().entry_count;
        readers.push_back(reader);
    }

    std::vector<std::unique_ptr<SSTableIterator>> owned_itrs;
    owned_itrs.reserve(readers.size());
    for (const auto& reader : readers) {
        owned_itrs.push_back(std::make_unique<SSTableIterator>(reader->iterator()));
    }

    /* borrows from readers above, heap stable tho */
    std::vector<InternalIterator*> iterators;
    iterators.reserve(owned_itrs.size());
    for (auto& it : owned_itrs) {
        iterators.push_back(it.get());
    }

    /* new sst writer */
    auto w = SSTableWriter::create(engine_, sst_path(data_dir_, next_sst_seq), possible_keys);
    if (!w.has_value()) {
        LOG_ERROR(Category::Compaction, "Failed to fetch footer from a writer for path={} got err={}",
                  sst_path(data_dir_, next_sst_seq), w.error().message);
        return Result<SSTableId>::err(w.error());
    }

    auto& writer = w.value();

    /* construct a merge iterator over itrs and add to writer */
    MergeIterator m_itr(iterators);
    for (m_itr.seek_to_first(); m_itr.valid(); m_itr.next()) {
        auto& key = m_itr.key();
        const auto& value = m_itr.value();

        /* skip tombstoned records if in full compaction */
        if (value.is_tombstone && is_full_compaction) continue;

        auto ar = writer.add(key, value);
        if (!ar.has_value()) {
            LOG_ERROR(Category::Compaction,
                      "Failed to add value to the new writer inside merge "
                      "iterator under path={}, got err={}",
                      sst_path(data_dir_, next_sst_seq), ar.error().message);
            return Result<SSTableId>::err(ar.error());
        }
    }

    /* flush and create this new file; writer.finish calls fsync on the file and
     * directory */
    if (auto f = writer.finish(); !f.has_value()) {
        LOG_ERROR(Category::Compaction, "Failed to finish and flush the new file under path={}, got={}",
                  sst_path(data_dir_, next_sst_seq), f.error().message);
        return Result<SSTableId>::err(f.error());
    }

    return Result<SSTableId>::ok(SSTableId{next_sst_seq});
}

}  // namespace enigmadb::dazzle
