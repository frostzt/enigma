/*
 * @file tempfile.hpp
 * @brief A temporary file wrapper that can create a tempfile, note
 * that this is used mainly for tests
 * @author frostzt
 * @date 2026-03-29
 */

#ifndef ENIGMA_DB_TEMPFILE_HPP
#define ENIGMA_DB_TEMPFILE_HPP

#include <cstring>
#include <string>
#include <unistd.h>

struct Tempfile {
  std::string path;
  int fd;

  Tempfile(const std::string &pattern_str) {
    char pattern[256];
    strncpy(pattern, pattern_str.c_str(), sizeof(pattern));
    fd = mkstemp(pattern);
    path = pattern;
  }

  ~Tempfile() {
    if (fd != -1) {
      ::close(fd);
    }
    unlink(path.c_str());
  }
};

#endif // ENIGMA_DB_TEMPFILE_HPP
