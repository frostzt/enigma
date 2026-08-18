
#include "enigmadb/storage/dazzle_db/wal/wal_reader.h"

#include <vector>

#include "enigmadb/base.h"
#include "enigmadb/encoding.h"
#include "enigmadb/storage/dazzle_db/wal/wal_record.h"

namespace enigmadb::dazzle {

Result<WalReader> WalReader::create(io::IOEngine& engine, const std::string& path) {
    auto open_result = engine.open(path, io::Mode::Read);
    if (!open_result.has_value()) {
        return Result<WalReader>::err(open_result.error());
    }

    auto& fh = open_result.value();
    WalReader reader(path, std::move(fh), engine);
    return Result<WalReader>::ok(std::move(reader));
}

Result<WalRecord> WalReader::next() {
    uint8_t header_buffer[9] = "";
    auto hread_result = engine_.read(fh_, 8, header_buffer, offset_);
    if (!hread_result.has_value()) {
        return Result<WalRecord>::err(hread_result.error());
    }

    auto bytes_read = hread_result.value();
    if (bytes_read == 0) {
        return Result<WalRecord>::err(Error{ErrorCode::ERR_EOF, "end of WAL file"});
    }

    auto body_length = decode_uint32(header_buffer, 0);
    if (body_length < 25) {
        return Result<WalRecord>::err(Error{ErrorCode::ERR_EOF, "wal reader encountered eof"});
    }

    std::vector<uint8_t> record_buffer(body_length + 8);
    auto full_read_result = engine_.read(fh_, body_length + 8, record_buffer.data(), offset_);
    if (!full_read_result.has_value()) {
        return Result<WalRecord>::err(full_read_result.error());
    }

    offset_ += 8 + body_length;

    auto deserialize_result = deserialize_wal_record(record_buffer.data(), record_buffer.size());
    if (!deserialize_result.has_value()) {
        return Result<WalRecord>::err(deserialize_result.error());
    }

    return Result<WalRecord>::ok(deserialize_result.value());
}

}  // namespace enigmadb::dazzle
