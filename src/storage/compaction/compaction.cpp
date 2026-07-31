#include "enigmadb/storage/compaction/compaction.h"

#include <memory>
#include <vector>

#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/common.h"
#include "enigmadb/storage/iterator.h"
#include "enigmadb/storage/merge_iterator.h"
#include "enigmadb/storage/sstable/sstable_common.h"
#include "enigmadb/storage/sstable/sstable_reader.h"
#include "enigmadb/storage/sstable/sstable_writer.h"

using namespace enigmadb::io;
using namespace enigmadb::storage::sstable;

namespace enigmadb::storage::compaction {

Compactor Compactor::create(IOEngine& engine, const std::string& data_dir) {
    return Compactor{engine, data_dir};
}

DoCompactResult Compactor::do_size_tiered_compact(
    const std::vector<SSTableId>& inputs, const uint64_t next_sst_seq,
    bool is_full_compaction) {
    uint64_t possible_keys = 0;

    /* open readers for all the sstable id inputs */
    std::vector<std::unique_ptr<SSTableReader>> readers;
    for (const auto i : inputs) {
        auto r = SSTableReader::create(engine_, sst_path(data_dir_, i.value));
        if (!r.has_value()) return DoCompactResult::err(r.err());

        /* get an estimate amount for next possible keys */
        auto& reader = r.value();
        auto f = reader.get_footer();
        if (!f.has_value()) return DoCompactResult::err(f.err());
        possible_keys += f.value().entry_count;

        readers.emplace_back(
            std::make_unique<SSTableReader>(std::move(reader)));
    }

    std::vector<std::unique_ptr<SSTableIterator>> owned_itrs;
    owned_itrs.reserve(readers.size());
    for (const auto& reader : readers) {
        owned_itrs.push_back(
            std::make_unique<SSTableIterator>(reader->iterator()));
    }

    /* borrows from readers above, heap stable tho */
    std::vector<Iterator*> iterators;
    iterators.reserve(owned_itrs.size());
    for (auto& it : owned_itrs) {
        iterators.push_back(it.get());
    }

    /* new sst writer */
    auto w = SSTableWriter::create(engine_, sst_path(data_dir_, next_sst_seq),
                                   possible_keys);
    if (!w.has_value()) return DoCompactResult::err(w.err());
    auto& writer = w.value();

    /* construct a merge iterator over itrs and add to writer */
    MergeIterator m_itr(iterators);
    for (m_itr.seek_to_first(); m_itr.valid(); m_itr.next()) {
        auto& key = m_itr.key();
        const auto& value = m_itr.value();

        /* skip tombstoned records if in full compaction */
        if (value.is_tombstone && is_full_compaction) continue;

        auto ar = writer.add(key, value);
        if (!ar.has_value()) return DoCompactResult::err(ar.err());
    }

    /* flush and create this new file; writer.finish calls fsync on the file and
     * directory */
    if (auto f = writer.finish(); !f.has_value()) {
        return DoCompactResult::err(f.err());
    }

    return DoCompactResult::ok(SSTableId{next_sst_seq});
}

}  // namespace enigmadb::storage::compaction
