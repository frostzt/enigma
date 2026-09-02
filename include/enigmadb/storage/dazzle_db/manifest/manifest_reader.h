#ifndef ENIGMADB_DAZZLEDB_MANIFEST_READER_H_
#define ENIGMADB_DAZZLEDB_MANIFEST_READER_H_

#include <string>

#include "enigmadb/base.h"
#include "enigmadb/buffer.h"
#include "enigmadb/io/io_engine.h"

namespace enigmadb::dazzle {

class ManifestReader {
   public:
    /// Creates a new ManifestReader and opens a new FileDescriptor reading from an existing manifest file and owns it
    static Result<ManifestReader> Open(io::IOEngine& engine);

   private:
    ManifestReader(io::IOEngine& engine, const std::string& path, io::FileHandle fh, BufferReader& bufreader)
        : path_(path), engine_(engine), fh_(std::move(fh)), buf_reader_(bufreader) {}

    /// Path to the current manifest file being read
    std::string path_;

    /// IO Engine through which all reads will be done
    io::IOEngine& engine_;

    /// Opens and owns the file handle for the current manifest file
    io::FileHandle fh_;

    /// Buffer for reading through the manifest
    BufferReader& buf_reader_;
};

}  // namespace enigmadb::dazzle

#endif  // ENIGMADB_DAZZLEDB_MANIFEST_WRITER_H_
