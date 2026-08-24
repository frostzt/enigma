#ifndef ENIGMADB_DAZZLEDB_MANIFEST_WRITER_H_
#define ENIGMADB_DAZZLEDB_MANIFEST_WRITER_H_

#include <string>

#include "enigmadb/base.h"
#include "enigmadb/buffer.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/dazzle_db/core/version_edit.h"

namespace enigmadb::dazzle {

class ManifestWriter {
   public:
    /// Creates a new ManifestWriter and opens a new FileDescriptor and owns it
    static Result<ManifestWriter> Open(io::IOEngine& engine, const std::string& path, const size_t prealloc = 100);

    /// Frames the VersionEdit encodes it and writes it to disk
    Result<void> append(const VersionEdit&);

   private:
    ManifestWriter(io::IOEngine& engine, const std::string& path, io::FileHandle fh, BufferWriter& bufwriter)
        : path_(path), engine_(engine), fh_(std::move(fh)), buf_writer_(bufwriter) {}

    /// Path to the current manifest file being written
    std::string path_;

    /// IO Engine controls the io for manifests
    io::IOEngine& engine_;

    /// Opens and owns file handle
    io::FileHandle fh_;

    /// Owns manifest writing end to end
    BufferWriter& buf_writer_;
};

}  // namespace enigmadb::dazzle

#endif  // ENIGMADB_DAZZLEDB_MANIFEST_WRITER_H_
