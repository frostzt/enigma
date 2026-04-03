#include "enigmadb/storage/wal/wal_writer.h"

#include <cassert>

#include "enigmadb/common/error.h"
#include "enigmadb/common/result.h"
#include "enigmadb/storage/wal/wal_record.h"

namespace enigmadb::storage::wal {

WalResult<WalWriter> WalWriter::create(io::IOEngine& engine,
                                       const std::string& path) {
    auto open_result = engine.open(path, io::Mode::Append);
    if (!open_result.has_value()) return open_result.err();
    auto& fh = open_result.value();
    WalWriter writer(path, std::move(fh), engine);
    return WalResult<WalWriter>::ok(std::move(writer));
}

WalResult<void> WalWriter::append(const WalRecord& record) {
    auto serialized = serialize_wal_record(record);
    assert(serialized.size() == get_record_size(record));

    auto append_result =
        engine_.append(fh_, serialized.data(), serialized.size());
    if (!append_result.has_value()) {
        return append_result.err();
    }

    return WalResult<void>::ok();
}

WalResult<void> WalWriter::sync() {
    auto sync_result = engine_.sync_data(fh_);
    if (!sync_result.has_value()) {
        return sync_result.err();
    }

    return WalResult<void>::ok();
}

}  // namespace enigmadb::storage::wal
