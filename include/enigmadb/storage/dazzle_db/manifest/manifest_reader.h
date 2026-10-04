#ifndef ENIGMADB_DAZZLEDB_MANIFEST_READER_H_
#define ENIGMADB_DAZZLEDB_MANIFEST_READER_H_

#include <memory>
#include <string>
#include <vector>

#include "enigmadb/base.h"
#include "enigmadb/common.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/dazzle_db/core/version_edit.h"

namespace enigmadb::dazzle {

class ManifestReader {
   public:
    /// Creates a new ManifestReader and opens a new FileDescriptor reading from an existing manifest file and owns it
    static Result<std::unique_ptr<ManifestReader>> Open(io::IOEngine& engine, const std::string& path);

    /// Reads the next VersionEdit change
    [[nodiscard]] Result<const VersionEdit> next();

   private:
    ManifestReader(io::IOEngine& engine, const std::string& path, io::FileHandle fh, std::vector<uint8_t> bytes)
        : pos_(0), bytes_(bytes), path_(path), engine_(engine), fh_(std::move(fh)) {}

    DELETE_CLASS_COPY(ManifestReader);

    /// Used by `next` to track individual VersionEdits
    size_t pos_;

    /// Raw bytes read from the manifest file buffer
    std::vector<uint8_t> bytes_;

    /// Path to the current manifest file being read
    std::string path_;

    /// IO Engine through which all reads will be done
    io::IOEngine& engine_;

    /// Opens and owns the file handle for the current manifest file
    io::FileHandle fh_;
};

}  // namespace enigmadb::dazzle

#endif  // ENIGMADB_DAZZLEDB_MANIFEST_WRITER_H_
