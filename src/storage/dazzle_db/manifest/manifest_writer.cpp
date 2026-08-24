#include "enigmadb/storage/dazzle_db/manifest/manifest_writer.h"

#include <string>

#include "enigmadb/base.h"
#include "enigmadb/buffer.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/log.h"
#include "enigmadb/storage/dazzle_db/core/version_edit.h"

namespace enigmadb::dazzle {

Result<ManifestWriter> ManifestWriter::Open(io::IOEngine& engine, const std::string& path, const size_t prealloc) {
    auto ores = engine.open(path, io::Mode::Write);
    if (!ores.has_value()) return Result<ManifestWriter>::err(ores.error());

    /* create required components */
    auto& fh = ores.value();
    BufferWriter bw(prealloc);
    ManifestWriter mw(engine, path, std::move(fh), bw);
    return Result<ManifestWriter>::ok(std::move(mw));
}

Result<void> ManifestWriter::append(const VersionEdit& ve) {
    /* frame and encode the version edit */
    auto fres = write_framed(buf_writer_, [&](BufferWriter& b) { encode_version_edit(b, ve); });
    if (!fres.has_value()) return Result<void>::err(fres.error());

    /* check if the Edit was successfully encoded */
    if (!buf_writer_.ok()) {
        LOG_ERROR(Category::DAZZLE_MANIFEST, "Last buffer failed to write successfully record already truncated");
        return Result<void>::err(buf_writer_.error());
    }

    /* commit to disk */
    auto ares = engine_.append(fh_, buf_writer_.data().data(), buf_writer_.size());
    if (!ares.has_value()) {
        LOG_ERROR(Category::DAZZLE_MANIFEST, "Manifest writer failed to append VersionEdit into manifest file");
        return Result<void>::err(ares.error());
    }

    /* fsync to disk */
    auto fsres = engine_.sync_data(fh_);
    if (!fsres.has_value()) {
        LOG_ERROR(Category::DAZZLE_MANIFEST, "Manifest writer failed to purge VersionEdit into disk");
        return Result<void>::err(fsres.error());
    }

    return Result<void>::ok();
}

}  // namespace enigmadb::dazzle
