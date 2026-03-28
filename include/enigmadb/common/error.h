/*
 * error.h -- Errors and error handling
 *
 * Author: frostzt
 * Date: 2026-03-20
 */

#ifndef ENIGMA_DB_ERROR_H
#define ENIGMA_DB_ERROR_H

#include <string>

enum class ErrorCode {
  UNEXPECTED_ERR = 0,

  /// Basic error types
  FILE_DESCRIPTER_ERR = 1,
  FSYNC_ERR = 2,
  CLOSE_ERR = 3,
};

struct Error {
  /* An internal code for representing what went wrong */
  ErrorCode code;
  std::string message;

  static Error unexpected(std::string message) {
    return Error{ErrorCode::UNEXPECTED_ERR, std::move(message)};
  }
};

#endif // ENIGMA_DB_ERROR_H
