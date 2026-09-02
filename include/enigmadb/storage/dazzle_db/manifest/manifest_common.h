#ifndef ENIGMADB_DAZZLEDB_MANIFEST_COMMON_H_
#define ENIGMADB_DAZZLEDB_MANIFEST_COMMON_H_

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace enigmadb::dazzle {

struct ManifestId {
    /// ID of the manifest file
    uint64_t value;

    bool operator<=>(const ManifestId& other) const = default;
    bool operator==(const ManifestId& other) const { return value == other.value; };
};

/// Generates a manifest file name a width of total 16 zeroes
inline std::string get_manifest_filename(const ManifestId id) {
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(16) << id.value << ".manifest";
    return ss.str();
}

}  // namespace enigmadb::dazzle

#endif  // ENIGMADB_DAZZLEDB_MANIFEST_READER_H_
