/**
 * @file wal_writer.h
 * @brief Append-only writer for the write-ahead log (WAL).
 *
 * WalWriter serializes WalRecord entries to a single WAL segment file
 * through an IOEngine backend. Instances are created exclusively via
 * the static factory WalWriter::create().
 *
 * @author frostzt
 * @date 2026-04-03
 */

#ifndef ENIGMA_DB_WAL_WRITER_H
#define ENIGMA_DB_WAL_WRITER_H

#include "enigmadb/io/io_engine.h"
#include "enigmadb/storage/dazzle_db/wal/wal_record.h"

namespace enigmadb::dazzle {

/**
 * @brief Append-only writer bound to a single WAL segment file.
 *
 * WalWriter is move-only (due to the underlying FileHandle) and holds
 * a non-owning reference to the IOEngine that performs the actual I/O.
 * All writes are appended sequentially; random-access writes are not
 * supported.
 *
 * Obtain instances through WalWriter::create(); the constructor is private.
 */
class WalWriter {
   private:
    std::string path_;
    io::FileHandle fh_;
    io::IOEngine& engine_;

    /**
     * @brief Private constructor; use WalWriter::create() instead.
     *
     * @param path    Filesystem path of the WAL segment.
     * @param fh      Open file handle (ownership is moved in).
     * @param engine  IOEngine used for all subsequent I/O on this segment.
     */
    WalWriter(const std::string& path, io::FileHandle fh, io::IOEngine& engine)
        : path_(path), fh_(std::move(fh)), engine_(engine) {}

   public:
    /**
     * @brief Serializes and appends a WAL record to the segment file.
     *
     * The record is encoded and written at the current end of the file.
     * This does not guarantee durability on its own; call sync() to
     * flush the data to stable storage.
     *
     * @param[in] record  The WAL record to append.
     * @return WalResult<void> — success, or an error if the write fails.
     */
    Result<void> append(const WalRecord& record);

    /**
     * @brief Flushes all buffered writes to stable storage via fdatasync.
     *
     * After a successful sync(), every record appended prior to this
     * call is guaranteed to be durable. Metadata updates (e.g. mtime)
     * may not be flushed — only file content is synchronized.
     * Typically called at transaction commit boundaries.
     *
     * @return WalResult<void> — success, or an error if the sync fails.
     */
    Result<void> sync();

    /**
     * @brief Factory that opens (or creates) a WAL segment and returns
     *        a ready-to-use WalWriter.
     *
     * The file is opened in append mode via @p engine. If the file
     * does not yet exist it will be created.
     *
     * @param engine  IOEngine to use for all I/O on this segment.
     * @param path    Filesystem path for the WAL segment file.
     * @return A WalWriter on success, or an error if the file cannot
     *         be opened.
     */
    static Result<WalWriter> create(io::IOEngine& engine, const std::string& path);
};

}  // namespace enigmadb::dazzle

#endif  // ENIGMA_DB_WAL_WRITER_H
