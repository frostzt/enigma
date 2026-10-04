#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "enigmadb/buffer.h"
#include "enigmadb/io/posix_io_engine.h"
#include "enigmadb/storage/dazzle_db/core/version_edit.h"

using namespace enigmadb;

int run(std::string filepath) {
    io::PosixIOEngine engine;
    auto fres = engine.open(filepath, io::Mode::Read);
    if (!fres.has_value()) {
        std::cerr << fres.error().message << std::endl;
        return 1;
    }

    auto& fh = fres.value();
    auto fz = engine.file_size(fh);
    if (!fz.has_value()) {
        std::cerr << fz.error().message << std::endl;
        return 1;
    }

    auto filesize = fz.value();

    size_t pos_ = 0;
    std::vector<uint8_t> bytes(filesize);
    auto read_result = engine.read(fh, filesize, bytes.data(), 0);

    BufferReader br(bytes.data(), filesize);

    while (pos_ <= filesize) {
        size_t bytes_read = 0;
        auto rf = read_framed<dazzle::VersionEdit>(
            br, [&](BufferReader& b) { return dazzle::decode_version_edit(b, bytes_read); });
        if (!rf.has_value()) return 1;

        pos_ += bytes_read;

        auto& ve = rf.value();

        std::cout << "--- [Added] ---\n";
        for (const auto a : ve.added) {
            std::cout << "ID: " << a.id.value << "\n";
            std::cout << "Entry Count: " << a.entry_count << "\n";
            std::cout << "Max Sequence: " << a.max_sequence << "\n";
            std::cout << "Size Bytes: " << a.size_bytes << "\n";
        }

        std::cout << "--- [Removed] ---\n";
        for (const auto r : ve.removed) {
            std::cout << "ID: " << r.value << "\n";
        }

        std::cout << "\n\n";

        std::cout << "--- [Next SST ID] ---\n";
        if (ve.next_sst_id.has_value()) {
            std::cout << ve.next_sst_id.value() << "\n";
        }
        std::cout << "\n" << std::endl;
    }

    return 0;
}

// manifestdump filename
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Error: Invalid usage:\n";
        std::cerr << "Usage: " << argv[0] << "<path_to_file>" << std::endl;
        return 1;
    }

    std::string filepath(argv[1]);

    std::filesystem::path target_file(filepath);
    if (!std::filesystem::exists(target_file)) {
        std::cerr << "Error: File does not exist -> " << filepath << std::endl;
        return 1;
    }

    return run(filepath);
}
