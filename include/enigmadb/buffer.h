#ifndef ENIGMADB_BUFFER_H_
#define ENIGMADB_BUFFER_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "enigmadb/base.h"
#include "enigmadb/crc32.h"
#include "enigmadb/log.h"
#include "error.h"

namespace enigmadb {

class BufferReader {
   public:
    BufferReader(const uint8_t* data, size_t length, std::optional<Error> error = std::nullopt)
        : data_(data), length_(length), pos_(0), err_(error) {
        if (data_ == nullptr && length_ > 0) {
            if (!err_.has_value()) {
                err_ = Error::bad_config("invalid data provided with a postive length");
            }
            length_ = 0;
        }
    };

    /* Reader can only be moved */
    BufferReader(BufferReader&&) = default;
    BufferReader& operator=(BufferReader&&) = default;

    /* Reader cannot be copied */
    BufferReader(const BufferReader&) = delete;
    BufferReader& operator=(const BufferReader&) = delete;

    size_t remaining() const;

    /// Reads 8 bytes from the buffer as unsigned integer and returns it as value
    [[nodiscard]] uint64_t read_u64();

    /// Reads 4 bytes from the buffer as unsigned integer and returns it as value
    [[nodiscard]] uint32_t read_u32();

    /// Reads 2 bytes from the buffer as unsigned integer and returns it as value
    [[nodiscard]] uint16_t read_u16();

    /// Reads 1 byte from the buffer as unsigned integer and returns it as value
    [[nodiscard]] uint8_t read_u8();

    /// Reads n bytes from the buffer and returns a non-owning view on top of it
    /// returns an empty span `{}` on invalid reads MUST BE validated using `ok()`
    [[nodiscard]] std::span<const uint8_t> read_bytes(size_t n);

    /// Skips n bytes
    void skip(size_t n);

    /// Returns a new BufferReader over the next `n` bytes advances the parent by n bytes, the new BufferReader
    /// owns the next n bytes if an error is encountered the child will contain that error and the caller is
    /// responsible for validating and making sure that the error encountered wasn't silently swallowed
    [[nodiscard]] BufferReader sub(size_t n);

    /// Return the current position of the buffer's cursor
    [[nodiscard]] size_t consumed() const;

    /// Returns weather the read so far was successful or not
    [[nodiscard]] bool ok() const;

    /// Returns the error encountered during the read, NOTE: if read pre-emptively when there is no `err_` WILL throw
    [[nodiscard]] const Error& error() const;

   private:
    /// Pointer to the buffer
    const uint8_t* data_;

    /// Length of the data buffer
    size_t length_;

    /// Current cursor
    size_t pos_;

    /// Holds an active error if encountered throughout the read buffer's lifecycle
    std::optional<Error> err_;
};

/// Raw buffer writer which writes binary directly into the memory buffer can be used to contruct anything
/// that requires writing raw binary buffers. Note that a poisoned BufferWriter that contains an error
/// found via `ok` will NOT perform any writes or patches
class BufferWriter {
   public:
    BufferWriter(size_t prealloc) : err_(std::nullopt) { data_.reserve(prealloc); }

    /* Writer can only be moved */
    BufferWriter(BufferWriter&&) = default;
    BufferWriter& operator=(BufferWriter&&) = default;

    /* Writer cannot be copied */
    BufferWriter(const BufferWriter&) = delete;
    BufferWriter& operator=(const BufferWriter&) = delete;

    /* --- core write methods --- */
    /// Writes a 8 bytes value at the current cursor position
    void write_u64(uint64_t value);

    /// Writes a 4 bytes value at the current cursor position
    void write_u32(uint32_t value);

    /// Writes a 2 bytes value at the current cursor position
    void write_u16(uint16_t value);

    /// Writes a 1 byte value at the current cursor position
    void write_u8(uint8_t value);

    /// Writes raw bytes into the internal vector
    void write_bytes(std::span<const uint8_t> value);

    /// Reserves n bytes writes zeros and returns its start offset, a poisoned BufferWriter would simply return 0
    [[nodiscard]] size_t reserve_slot(size_t n);

    /* --- core patching methods --- */
    /// Patches a 8 bytes value at the provided offset
    void patch_u64(size_t offset, uint64_t value);

    /// Patches a 4 bytes value at the provided offset
    void patch_u32(size_t offset, uint32_t value);

    /// Patches a 2 bytes value at the provided offset
    void patch_u16(size_t offset, uint16_t value);

    /// Patches a 1 byte value at the provided offset
    void patch_u8(size_t offset, uint8_t value);

    /// Drops everything post the provided byte mark
    void truncate(size_t mark, bool clear_error = true);

    /* --- checking methods --- */
    /// Returns weather the read so far was successful or not
    [[nodiscard]] bool ok() const;

    /// Returns the error encountered during patches, NOTE: if read pre-emptively when there is no `err_` WILL throw,
    /// writes won't throw
    [[nodiscard]] const Error& error() const;

    /// Returns the current size of the internal data vector
    [[nodiscard]] size_t size() const;

    /// Returns the current capacity of the internal vector
    [[nodiscard]] size_t capacity() const;

    /* --- core methods --- */
    /// Clears the internal data vector and clears the held error as well
    void clear();

    /// Returns a non-owning view of the current internal data vector, the data is NON-OWNING if the internal
    /// vector is reallocated post a write using the old span would be a UB
    [[nodiscard]] std::span<const uint8_t> data() const;

    /// Reserves the capacity for this write buffer, ALWAYS prefer providing the ctor with the capacity
    void reserve(size_t alloc);

   private:
    /// Current data vector holding the entire data
    std::vector<uint8_t> data_;

    /// Holds an active error if encountered throughout the write buffer's lifecycle
    std::optional<Error> err_;
};

template <typename Encode>
[[nodiscard]] Result<void> write_framed(BufferWriter& bw, Encode&& encode) {
    if (!bw.ok()) return Result<void>::err(bw.error());

    const size_t hdr = bw.reserve_slot(8);
    const size_t body_begin = hdr + 8;

    /* encode the body */
    encode(bw);
    if (!bw.ok()) {
        auto err = bw.error();
        bw.truncate(hdr);
        return Result<void>::err(err);
    };

    const size_t body_len = bw.size() - body_begin;
    if (body_len > UINT32_MAX) {
        LOG_ERROR(Category::General,
                  "Encountered body length for a buffer being read from the frame reader to be more than UINT32_MAX");
        bw.truncate(hdr);
        return Result<void>::err(Error::buffer_too_large("Encountered body length too large"));
    }

    bw.patch_u32(hdr, static_cast<uint32_t>(body_len));
    if (!bw.ok()) {
        auto err = bw.error();
        bw.truncate(hdr);
        return Result<void>::err(err);
    }

    const auto buf = bw.data();
    bw.patch_u32(hdr + 4, compute_crc_32(buf.data() + body_begin, body_len));
    if (!bw.ok()) {
        auto err = bw.error();
        bw.truncate(hdr);
        return Result<void>::err(err);
    }

    return Result<void>::ok();
}

template <typename T, typename Decode>
[[nodiscard]] Result<T> read_framed(BufferReader& br, Decode&& decode) {
    if (!br.ok()) return Result<T>::err(br.error());

    const uint32_t len = br.read_u32();
    const uint32_t checksum = br.read_u32();
    const auto body = br.read_bytes(len);
    if (!br.ok()) return Result<T>::err(Error::incomplete_record("Received bytes fewer than claimed"));
    if (compute_crc_32(body.data(), len) != checksum) {
        return Result<T>::err(Error::checksum_mismatch("checksum mismatch"));
    }

    BufferReader body_reader(body.data(), len);
    Result<T> out = decode(body_reader);
    if (!out.has_value()) return out;

    if (!body_reader.ok()) {
        return Result<T>::err(Error::incomplete_record("The body is shorter than the record claimed"));
    }
    if (body_reader.remaining() != 0) {
        return Result<T>::err(Error::corruption("The body is longer than the record claimed"));
    }
    return out;
}

}  // namespace enigmadb

#endif  // ENIGMADB_BUFFER_H_
