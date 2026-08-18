#include <iostream>
#include <string>

#include "enigmadb/catalog/key_encoding.h"
#include "enigmadb/io/posix_io_engine.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_iterator.h"
#include "enigmadb/storage/dazzle_db/sstable/sstable_reader.h"
#include "enigmadb/utils.h"

using namespace enigmadb;

int run(std::string filepath) {
    io::PosixIOEngine engine;
    auto rr = dazzle::SSTableReader::create(engine, filepath);
    if (!rr.has_value()) {
        std::cout << rr.error().message << "\n";
        return 1;
    }

    auto& reader = rr.value();

    std::cout << "------------ [SSTable] -- " << filepath << " -- ------------\n";
    std::cout << "------------ [FOOTER BLOCK] ------------\n";

    auto fv = reader.get_footer();
    if (!fv.has_value()) {
        std::cout << fv.error().message << "\n";
        return 1;
    }

    auto footer = fv.value();
    std::cout << "index_block_offset: " << footer.index_block_offset
              << "\nindex_block_size: " << footer.index_block_size
              << "\nfilter_block_offset: " << footer.filter_block_offset
              << "\nfilter_block_size: " << footer.filter_block_size << "\nentry_count: " << footer.entry_count
              << "\nformat_version: " << footer.format_version << "\nhighest_sequence: " << footer.highest_sequence
              << "\nsize_bytes" << footer.size_bytes << "\n\n\n";

    std::cout << "------------ [DATA BLOCK] ------------\n" << std::endl;
    auto itr = std::make_unique<dazzle::SSTableIterator>(std::make_shared<dazzle::SSTableReader>(std::move(reader)));

    for (itr->seek_to_first(); itr->valid(); itr->next()) {
        auto v = itr->value();

        auto decomposed = catalog::decode_composite_key(itr->key().bytes());
        if (!decomposed.has_value()) return 1;

        auto compkey = decomposed.value();

        std::cout << "[ENCODED] key: " << bytes_to_string(compkey.partition_key) << "_"
                  << bytes_to_string(compkey.clustering_key) << "_" << bytes_to_string(compkey.column_name) << "   ";
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
