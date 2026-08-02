#include "enigmadb/io/posix_io_engine.h"

#include <cstring>
#include <string>

#include "enigmadb/error.h"
#include "enigmadb/io/io_engine.h"
#include "enigmadb/tempfile.h"
#include "gtest/gtest.h"

using namespace enigmadb;

TEST(POSIX_IO_Engine, open_non_existent_file) {
    io::PosixIOEngine engine;
    auto result = engine.open("i_do_not_exist.txt", io::Mode::Read);

    ASSERT_FALSE(result.has_value());
    auto& err = result.error();

    ASSERT_EQ(err.code, ErrorCode::FILE_DESCRIPTOR_ERR);
    EXPECT_STREQ("No such file or directory", err.message.c_str());
}

TEST(POSIX_IO_Engine, create_write_close) {
    Tempfile testfile("tempfile-XXXXXX");
    uint8_t string_buffer[] = "sourav";

    {
        io::PosixIOEngine engine;
        auto open_result = engine.open(testfile.path, io::Mode::Append);

        ASSERT_TRUE(open_result.has_value());
        auto& fh = open_result.value();

        auto append_result = engine.append(fh, string_buffer, 6);
        ASSERT_TRUE(append_result.has_value());

        auto val = append_result.value();
        ASSERT_EQ(val, 6);
    }

    {
        io::PosixIOEngine engine;
        auto open_result = engine.open(testfile.path, io::Mode::Read);

        ASSERT_TRUE(open_result.has_value());
        auto& fh = open_result.value();

        uint8_t read_buf[7] = "";
        auto read_result = engine.read(fh, 6, read_buf, 0);
        ASSERT_TRUE(read_result.has_value());

        ASSERT_EQ(memcmp(string_buffer, read_buf, 6), 0);
    }
}

TEST(POSIX_IO_Engine, read_past_eof) {
    Tempfile testfile("tempfile-XXXXXX");
    uint8_t string_buffer[] = "sourav";

    {
        io::PosixIOEngine engine;
        auto open_result = engine.open(testfile.path, io::Mode::Append);

        ASSERT_TRUE(open_result.has_value());
        auto& fh = open_result.value();

        /* wrote 6 bytes */
        auto append_result = engine.append(fh, string_buffer, 6);
        ASSERT_TRUE(append_result.has_value());
        auto val = append_result.value();
        ASSERT_EQ(val, 6);
    }

    {
        io::PosixIOEngine engine;
        auto open_result = engine.open(testfile.path, io::Mode::Read);

        ASSERT_TRUE(open_result.has_value());
        auto& fh = open_result.value();

        uint8_t read_buf[101] = "";
        /* read 100 bytes instead of 6 triggers an eof */
        auto read_result = engine.read(fh, 100, read_buf, 0);
        ASSERT_TRUE(read_result.has_value());
        ASSERT_EQ(read_result.value(), 6);
    }
}

TEST(POSIX_IO_Engine, read_from_offset) {
    Tempfile testfile("tempfile-XXXXXX");
    uint8_t string_buffer[] = "helloworld";

    {
        io::PosixIOEngine engine;
        auto open_result = engine.open(testfile.path, io::Mode::Append);

        ASSERT_TRUE(open_result.has_value());
        auto& fh = open_result.value();

        /* wrote 6 bytes */
        auto append_result = engine.append(fh, string_buffer, 10);
        ASSERT_TRUE(append_result.has_value());
        auto val = append_result.value();
        ASSERT_EQ(val, 10);
    }

    {
        io::PosixIOEngine engine;
        auto open_result = engine.open(testfile.path, io::Mode::Read);

        ASSERT_TRUE(open_result.has_value());
        auto& fh = open_result.value();

        uint8_t read_buf[7] = "";
        auto read_result = engine.read(fh, 5, read_buf, 5);
        ASSERT_TRUE(read_result.has_value());
        // ASSERT_EQ("world", read_buf);
    }
}

TEST(POSIX_IO_Engine, sync_data_succeeds) {
    Tempfile testfile("tempfile-XXXXXX");
    uint8_t string_buffer[] = "helloworld";

    io::PosixIOEngine engine;
    auto open_result = engine.open(testfile.path, io::Mode::Append);

    ASSERT_TRUE(open_result.has_value());
    auto& fh = open_result.value();

    /* wrote 6 bytes */
    auto append_result = engine.append(fh, string_buffer, 10);
    ASSERT_TRUE(append_result.has_value());
    auto val = append_result.value();
    ASSERT_EQ(val, 10);

    auto sync_result = engine.sync_data(fh);
    ASSERT_TRUE(sync_result.has_value());
}

TEST(POSIX_IO_Engine, sync_all_succeeds) {
    Tempfile testfile("tempfile-XXXXXX");
    uint8_t string_buffer[] = "helloworld";

    io::PosixIOEngine engine;
    auto open_result = engine.open(testfile.path, io::Mode::Append);

    ASSERT_TRUE(open_result.has_value());
    auto& fh = open_result.value();

    /* wrote 6 bytes */
    auto append_result = engine.append(fh, string_buffer, 10);
    ASSERT_TRUE(append_result.has_value());
    auto val = append_result.value();
    ASSERT_EQ(val, 10);

    auto sync_result = engine.sync_all(fh);
    ASSERT_TRUE(sync_result.has_value());
}

TEST(POSIX_IO_Engine, multi_appends) {
    Tempfile testfile("tempfile-XXXXXX");
    uint8_t hello[] = "hello";
    uint8_t world[] = "world";

    {
        io::PosixIOEngine engine;
        auto open_result = engine.open(testfile.path, io::Mode::Append);

        ASSERT_TRUE(open_result.has_value());
        auto& fh = open_result.value();

        auto hello_append_result = engine.append(fh, hello, 6);
        ASSERT_TRUE(hello_append_result.has_value());
        auto hval = hello_append_result.value();
        ASSERT_EQ(hval, 6);

        auto world_append_result = engine.append(fh, world, 6);
        ASSERT_TRUE(world_append_result.has_value());
        auto wval = world_append_result.value();
        ASSERT_EQ(wval, 6);
    }

    {
        io::PosixIOEngine engine;
        auto open_result = engine.open(testfile.path, io::Mode::Read);

        ASSERT_TRUE(open_result.has_value());
        auto& fh = open_result.value();

        uint8_t read_buf[11] = "";
        auto read_result = engine.read(fh, 10, read_buf, 0);
        ASSERT_TRUE(read_result.has_value());
        // ASSERT_EQ("helloworld", read_buf);
    }
}
