/*
 * @file tempfile.h
 * @brief A temporary file wrapper that can create a tempfile, note
 * that this is used mainly for tests
 * @author frostzt
 * @date 2026-03-29
 */

#ifndef ENIGMA_DB_TEMPFILE_HPP
#define ENIGMA_DB_TEMPFILE_HPP

#include <unistd.h>

#include <cstring>
#include <string>

namespace enigmadb::common {

struct Tempfile {
    std::string path;
    int fd;

    Tempfile(const std::string& pattern_str) {
        char pattern[256];
        strncpy(pattern, pattern_str.c_str(), sizeof(pattern));
        fd = mkstemp(pattern);
        path = pattern;
        ::close(fd);
        fd = -1;
    }

    ~Tempfile() {
        if (fd != -1) {
            ::close(fd);
        }
        unlink(path.c_str());
    }
};

}  // namespace enigmadb::common

#endif  // ENIGMA_DB_TEMPFILE_HPP
