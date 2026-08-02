/*
 * error.h -- Errors and error handling
 *
 * Author: frostzt
 * Date: 2026-03-20
 */

#ifndef ENIGMA_DB_ERROR_H
#define ENIGMA_DB_ERROR_H

#include <execinfo.h>

#include <cstddef>
#include <string>

namespace enigmadb {

enum class ErrorCode {
    NONE = -1,

    UNEXPECTED_ERR = 0,

    /// Basic error types
    FILE_DESCRIPTOR_ERR = 1,
    FSYNC_ERR = 2,
    CLOSE_ERR = 3,
    BAD_CONFIG = 4,
    WRITE_ERR = 5,
    READ_ERR = 6,
    READ_OUT_OF_RANGE = 7,
    ERR_EOF = 8,
    FSTAT_ERR = 9,
    BAD_MAGIC = 10,
    BAD_FILE = 11,
    STRUCTURE_EMPTY = 12,
};

struct Error {
    /* An internal code for representing what went wrong */
    ErrorCode code;
    std::string message;

    static Error unexpected(std::string message) {
        return Error{ErrorCode::UNEXPECTED_ERR, std::move(message)};
    }

    static Error bad_config(std::string message) {
        return Error{ErrorCode::BAD_CONFIG, std::move(message)};
    }
};

[[noreturn]] void _server_panic_impl(const char* file, int line,
                                     const std::string& msg);

#define server_panic(msg) _server_panic_impl(__FILE__, __LINE__, msg)

#define server_assert(condition)                           \
    do {                                                   \
        if (!(condition)) {                                \
            server_panic("Assertion failed: " #condition); \
        }                                                  \
    } while (0)

}  // namespace enigmadb

#endif  // ENIGMA_DB_ERROR_H
