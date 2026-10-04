#include "enigmadb/storage/dazzle_db/manifest/manifest_reader.h"

#include <memory>
#include <vector>

#include "enigmadb/base.h"
#include "enigmadb/buffer.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/dazzle_db/core/version_edit.h"

namespace enigmadb::dazzle {

Result<std::unique_ptr<ManifestReader>> ManifestReader::Open(io::IOEngine& engine, const std::string& path) {
    auto ores = engine.open(path, io::Mode::Read);
    if (!ores.has_value()) return Result<std::unique_ptr<ManifestReader>>::err(ores.error());

    /* get the file handler */
    auto& fh = ores.value();
    auto fres = engine.file_size(fh);
    if (!fres.has_value()) return Result<std::unique_ptr<ManifestReader>>::err(fres.error());

    auto filesize = fres.value();

    std::vector<uint8_t> bytes(filesize);
    auto read_result = engine.read(fh, filesize, bytes.data(), 0);
    if (!read_result.has_value()) return Result<std::unique_ptr<ManifestReader>>::err(read_result.error());

    auto mr = std::unique_ptr<ManifestReader>(new ManifestReader(engine, path, std::move(fh), std::move(bytes)));
    return Result<std::unique_ptr<ManifestReader>>::ok(std::move(mr));
}

Result<const VersionEdit> ManifestReader::next() {
    // Early check to avoid reading beyond the available bytes
    if (pos_ + 1 >= bytes_.size()) return Result<const VersionEdit>::err(Error::err_eof("EOF"));

    BufferReader br(bytes_.data() + pos_, bytes_.size() - pos_);
    size_t read_bytes = 0;
    auto rres = read_framed<VersionEdit>(br, [&](BufferReader& b) { return decode_version_edit(b, read_bytes); });
    if (!rres.has_value()) return Result<const VersionEdit>::err(rres.error());

    pos_ += read_bytes;

    return Result<const VersionEdit>::ok(rres.value());
}

}  // namespace enigmadb::dazzle
