/*
 * @file tempdir.h
 * @brief A temporary dir wrapper that can create a tempdir, note
 * that this is used mainly for tests
 * @author frostzt
 * @date 2026-05-19
 */

#ifndef ENIGMA_DB_TEMPDIR_H
#define ENIGMA_DB_TEMPDIR_H

#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <string>

namespace enigmadb::common {

struct Tempdir {
    std::string path;

    Tempdir(const std::string& p) : path(p) {
        std::filesystem::create_directory(path);
    }

    ~Tempdir() { std::filesystem::remove_all(path); }
};

}  // namespace enigmadb::common

#endif  // ENIGMA_DB_TEMPDIR_H
