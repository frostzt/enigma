#include <iostream>
#include <string>
#include <vector>

#include "enigmadb/common/utils.h"
#include "enigmadb/io/posix_io_engine.h"
#include "enigmadb/storage/key_encoding.h"
#include "enigmadb/storage/sstable/sstable_reader.h"

using namespace enigmadb::storage::sstable;
using namespace enigmadb::storage;
using namespace enigmadb::io;
using namespace enigmadb::common;

int run(std::string filepath) {
    PosixIOEngine engine;
    auto rr = SSTableReader::create(engine, filepath);
    if (!rr.has_value()) {
        std::cout << rr.err().message << "\n";
        return 1;
    }

    auto& reader = rr.value();

    std::cout << "------------ [SSTable] -- " << filepath
              << " -- ------------\n";
    std::cout << "------------ [FOOTER BLOCK] ------------\n";

    auto fv = reader.get_footer();
    if (!fv.has_value()) {
        std::cout << fv.err().message << "\n";
        return 1;
    }

    auto footer = fv.value();
    std::cout << "index_block_offset: " << footer.index_block_offset
              << "\nindex_block_size: " << footer.index_block_size
              << "\nfilter_block_offset: " << footer.filter_block_offset
              << "\nfilter_block_size: " << footer.filter_block_size
              << "\nentry_count: " << footer.entry_count
              << "\nformat_version: " << footer.format_version
              << "\nhighest_sequence: " << footer.highest_sequence << "\n\n\n";

    std::cout << "------------ [DATA BLOCK] ------------\n" << std::endl;
    auto itr = reader.iterator();

    for (itr.seek_to_first(); itr.valid(); itr.next()) {
        auto v = itr.value();

        std::vector<uint8_t> pkey;
        std::vector<uint8_t> ckey;
        std::string cname;

        decode_composite_key(itr.key(), pkey, ckey, cname);

        std::cout << "[ENCODED] key: " << bytes_to_string(pkey) << "_"
                  << bytes_to_string(ckey) << "_" << cname << "   ";
        std::cout << "val: " << bytes_to_string(v.data) << "  ";
        std::cout << "tom: " << v.is_tombstone << "  ";
        std::cout << "seq: " << v.sequence << std::endl;
    }

    std::cout << "\n\n\n";

    return 0;
}

// sstdump filename
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Error: Invalid usage.\n";
        std::cerr << "Usage: " << argv[0] << " <path_to_file>\n";
        return 1;
    }

    std::string filePath(argv[1]);

    std::filesystem::path targetFile(filePath);
    if (!std::filesystem::exists(targetFile)) {
        std::cerr << "Error: File does not exist -> " << filePath << "\n";
        return 1;
    }

    return run(filePath);
}
