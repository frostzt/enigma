#include "enigmadb/storage/wal/wal_reader.h"

#include <vector>

#include "enigmadb/common/encoding.h"
#include "enigmadb/common/error.h"
#include "enigmadb/storage/wal/wal_record.h"

namespace enigmadb::storage::wal {

WalResult<WalReader> WalReader::create(io::IOEngine& engine,
                                       const std::string& path) {
    auto open_result = engine.open(path, io::Mode::Read);
    if (!open_result.has_value()) return open_result.err();
    auto& fh = open_result.value();
    WalReader reader(path, std::move(fh), engine);
    return WalResult<WalReader>::ok(std::move(reader));
}

WalResult<WalRecord> WalReader::next() {
    uint8_t header_buffer[9] = "";
    auto hread_result = engine_.read(fh_, 8, header_buffer, offset_);
    if (!hread_result.has_value()) return hread_result.err();

    auto bytes_read = hread_result.value();
    if (bytes_read == 0) {
        return WalResult<WalRecord>::err(
            common::Error{common::ErrorCode::ERR_EOF, "end of WAL file"});
    }

    auto body_length = common::decode_uint32(header_buffer, 0);
    if (body_length < 25) {
        return WalResult<WalRecord>::err(common::Error{
            common::ErrorCode::ERR_EOF, "wal reader encountered eof"});
    }

    std::vector<uint8_t> record_buffer(body_length + 8);
    auto full_read_result =
        engine_.read(fh_, body_length + 8, record_buffer.data(), offset_);
    if (!full_read_result.has_value()) {
        return full_read_result.err();
    }

    offset_ += 8 + body_length;

    auto deserialize_result =
        deserialize_wal_record(record_buffer.data(), record_buffer.size());
    if (!deserialize_result.has_value()) {
        return deserialize_result.err();
    }

    return WalResult<WalRecord>::ok(deserialize_result.value());
}

}  // namespace enigmadb::storage::wal
