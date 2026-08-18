#include "enigmadb/storage/dazzle_db/wal/wal_writer.h"

#include <cassert>

#include "enigmadb/base.h"
#include "enigmadb/storage/dazzle_db/wal/wal_record.h"

namespace enigmadb::dazzle {

Result<WalWriter> WalWriter::create(io::IOEngine& engine, const std::string& path) {
    auto open_result = engine.open(path, io::Mode::Append);
    if (!open_result.has_value()) {
        return Result<WalWriter>::err(open_result.error());
    }

    auto& fh = open_result.value();
    WalWriter writer(path, std::move(fh), engine);
    return Result<WalWriter>::ok(std::move(writer));
}

Result<void> WalWriter::append(const WalRecord& record) {
    auto serialized = serialize_wal_record(record);
    assert(serialized.size() == get_record_size(record));

    auto append_result = engine_.append(fh_, serialized.data(), serialized.size());
    if (!append_result.has_value()) {
        return Result<void>::err(append_result.error());
    }

    return Result<void>::ok();
}

Result<void> WalWriter::sync() {
    auto sync_result = engine_.sync_data(fh_);
    if (!sync_result.has_value()) {
        return Result<void>::err(sync_result.error());
    }

    return Result<void>::ok();
}

}  // namespace enigmadb::dazzle
