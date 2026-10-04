#include <gtest/gtest.h>

#include "enigmadb/io/posix_io_engine.h"
#include "enigmadb/storage/dazzle_db/core/version_edit.h"
#include "enigmadb/storage/dazzle_db/manifest/manifest_reader.h"
#include "enigmadb/storage/dazzle_db/manifest/manifest_writer.h"
#include "enigmadb/tempfile.h"

using namespace enigmadb;

namespace {

dazzle::VersionEdit make_edit(size_t r, size_t a, std::optional<uint64_t> next_sst_id) {
    std::vector<dazzle::SSTableId> removed;
    removed.reserve(r);
    for (size_t i = 0; i < r; i++) {
        removed.push_back(dazzle::SSTableId{100 + i});
    }

    std::vector<dazzle::SSTableMeta> added;
    added.reserve(a);
    for (size_t i = 0; i < a; i++) {
        added.push_back(dazzle::SSTableMeta{
            .id = dazzle::SSTableId{200 + i}, .size_bytes = 1000 + i, .entry_count = 500 + i, .max_sequence = 50 + i});
    }

    return dazzle::VersionEdit{std::move(removed), std::move(added), next_sst_id};
}

}  // namespace

TEST(Manifest, write_and_read_entry) {
    io::PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto writer_result = dazzle::ManifestWriter::Open(engine, testfile.path);
    ASSERT_TRUE(writer_result.has_value());

    auto& writer = writer_result.value();

    // Write 1 VersionEdit and try and read it back
    size_t added = 6;
    size_t removed = 3;
    size_t next_id = 3;

    auto ve = make_edit(removed, added, next_id);
    ASSERT_TRUE(writer->append(ve).has_value());

    auto reader_result = dazzle::ManifestReader::Open(engine, testfile.path);
    ASSERT_TRUE(reader_result.has_value());

    auto& reader = reader_result.value();

    auto read_result = reader->next();
    ASSERT_TRUE(read_result.has_value());

    auto& result = read_result.value();

    ASSERT_EQ(result.added.size(), added);
    ASSERT_EQ(result.removed.size(), removed);
    ASSERT_TRUE(result.next_sst_id.has_value());
    ASSERT_EQ(result.next_sst_id.value(), next_id);
}

TEST(Manifest, write_and_read_multiple_entries) {
    io::PosixIOEngine engine;
    Tempfile testfile("tempfile-XXXXXX");

    auto writer_result = dazzle::ManifestWriter::Open(engine, testfile.path);
    ASSERT_TRUE(writer_result.has_value());

    auto& writer = writer_result.value();

    // Write multiple VersionEdit and try and read it back
    for (size_t i = 0; i < 50; i++) {
        auto ve = make_edit(i + 1, i + 2, i + 3);
        ASSERT_TRUE(writer->append(ve).has_value());
    }

    auto reader_result = dazzle::ManifestReader::Open(engine, testfile.path);
    ASSERT_TRUE(reader_result.has_value());

    auto& reader = reader_result.value();

    // Above we created 50 entires we MUST BE ABLE to read atleast 50 records back
    for (size_t i = 0; i < 50; i++) {
        auto read_result = reader->next();
        ASSERT_TRUE(read_result.has_value());

        auto& result = read_result.value();

        ASSERT_EQ(result.added.size(), i + 2);
        ASSERT_EQ(result.removed.size(), i + 1);
        ASSERT_TRUE(result.next_sst_id.has_value());
        ASSERT_EQ(result.next_sst_id.value(), i + 3);
    }
}
