/*
 * error.h -- Errors and error handling
 *
 * Author: frostzt
 * Date: 2026-03-20
 */

#ifndef ENIGMA_DB_ERROR_H
#define ENIGMA_DB_ERROR_H

#include <execinfo.h>

#include <cstdint>
#include <string>

namespace enigmadb {

#define ERROR_CODES(X)                             \
    /*  Enum Name | Helper Function | Value */     \
    X(NONE, none, -1)                              \
    /* Basic error types */                        \
    X(UNEXPECTED_ERR, unexpected, 0)               \
    X(FILE_DESCRIPTOR_ERR, file_descriptor_err, 1) \
    X(FSYNC_ERR, fsync_err, 2)                     \
    X(CLOSE_ERR, close_err, 3)                     \
    X(BAD_CONFIG, bad_config, 4)                   \
    X(WRITE_ERR, write_err, 5)                     \
    X(READ_ERR, read_err, 6)                       \
    X(READ_OUT_OF_RANGE, read_out_of_range, 7)     \
    X(ERR_EOF, err_eof, 8)                         \
    X(FSTAT_ERR, fstat_err, 9)                     \
    X(BAD_MAGIC, bad_magic, 10)                    \
    X(BAD_FILE, bad_file, 11)                      \
    X(STRUCTURE_EMPTY, structure_empty, 12)        \
    X(CHECKSUM_MISMATCH, checksum_mismatch, 13)    \
    X(CORRUPTION, corruption, 14)                  \
    X(INCOMPLETE_RECORD, incomplete_record, 15)    \
    /* Concurrency and Version errors */           \
    X(STALE_VERSION, stale_version, 1001)

enum class ErrorCode : int32_t {
#define DEFINE_ENUM_ENTRY(enum_name, func_name, val) enum_name = val,
    ERROR_CODES(DEFINE_ENUM_ENTRY)
#undef DEFINE_ENUM_ENTRY
};

struct Error {
    /* An internal code for representing what went wrong */
    ErrorCode code;
    std::string message;

    // static ctor
#define DEFINE_STATIC_HELPER(enum_name, func_name, val) \
    static Error func_name(std::string msg) { return Error{ErrorCode::enum_name, std::move(msg)}; }
    ERROR_CODES(DEFINE_STATIC_HELPER)
#undef DEFINE_STATIC_HELPER

    // checker
#define DEFINE_CHECKER(enum_name, func_name, val) \
    bool is_##func_name() const { return code == ErrorCode::enum_name; }
    ERROR_CODES(DEFINE_CHECKER)
#undef DEFINE_CHECKER
};

[[noreturn]] void _server_panic_impl(const char* file, int line, const std::string& msg);

#define server_panic(msg) _server_panic_impl(__FILE__, __LINE__, msg)

#define server_assert(condition)                           \
    do {                                                   \
        if (!(condition)) {                                \
            server_panic("Assertion failed: " #condition); \
        }                                                  \
    } while (0)

}  // namespace enigmadb

#endif  // ENIGMA_DB_ERROR_H
