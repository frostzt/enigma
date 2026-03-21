/*
 * error.h -- Errors and error handling
 *
 * Author: frostzt
 * Date: 2026-03-20
 */

#ifndef ENIGMA_DB_ERROR_H
#define ENIGMA_DB_ERROR_H

#include <string>

enum ErrorCode {
  UNEXPECTED_ERR = 0,
};

struct Error {
  /* An internal code for representing what went wrong */
  ErrorCode code;
  std::string &message;
};

#endif // ENIGMA_DB_ERROR_H
