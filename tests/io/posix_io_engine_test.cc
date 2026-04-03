#include "enigmadb/io/posix_io_engine.hpp"

#include <string>

#include "enigmadb/common/error.h"
#include "enigmadb/common/tempfile.h"
#include "enigmadb/io/io_engine.hpp"
#include "gtest/gtest.h"

using namespace enigmadb::io;
using namespace enigmadb::common;

TEST(POSIX_IO_Engine, open_non_existent_file) {
    PosixIOEngine engine;
    auto result = engine.open("i_do_not_exist.txt", Mode::Read);

    ASSERT_FALSE(result.has_value());
    auto& err = result.err();

    ASSERT_EQ(err.code, ErrorCode::FILE_DESCRIPTOR_ERR);
    EXPECT_STREQ("No such file or directory", err.message.c_str());
}

TEST(POSIX_IO_Engine, create_write_close) {
    Tempfile testfile("tempfile-XXXXXX");
    std::string string_buffer = "sourav";

    {
        PosixIOEngine engine;
        auto open_result = engine.open(testfile.path, Mode::Append);

        ASSERT_TRUE(open_result.has_value());
        auto& fh = open_result.value();

        auto append_result =
            engine.append(fh, string_buffer.data(), string_buffer.size());
        ASSERT_TRUE(append_result.has_value());

        auto val = append_result.value();
        ASSERT_EQ(val, string_buffer.size());
    }

    {
        PosixIOEngine engine;
        auto open_result = engine.open(testfile.path, Mode::Read);

        ASSERT_TRUE(open_result.has_value());
        auto& fh = open_result.value();

        std::string read_buf(string_buffer.size(), '\0');
        auto read_result =
            engine.read(fh, string_buffer.size(), read_buf.data(), 0);
        ASSERT_TRUE(read_result.has_value());

        ASSERT_EQ(string_buffer, read_buf);
    }
}

TEST(POSIX_IO_Engine, read_past_eof) {
    Tempfile testfile("tempfile-XXXXXX");
    std::string string_buffer = "sourav";

    {
        PosixIOEngine engine;
        auto open_result = engine.open(testfile.path, Mode::Append);

        ASSERT_TRUE(open_result.has_value());
        auto& fh = open_result.value();

        /* wrote 6 bytes */
        auto append_result =
            engine.append(fh, string_buffer.data(), string_buffer.size());
        ASSERT_TRUE(append_result.has_value());
        auto val = append_result.value();
        ASSERT_EQ(val, string_buffer.size());
    }

    {
        PosixIOEngine engine;
        auto open_result = engine.open(testfile.path, Mode::Read);

        ASSERT_TRUE(open_result.has_value());
        auto& fh = open_result.value();

        std::string read_buf(100, '\0');
        /* read 100 bytes instead of 6 triggers an eof */
        auto read_result = engine.read(fh, 100, read_buf.data(), 0);
        ASSERT_TRUE(read_result.has_value());
        ASSERT_EQ(read_result.value(), 6);
    }
}

TEST(POSIX_IO_Engine, read_from_offset) {
    Tempfile testfile("tempfile-XXXXXX");
    std::string string_buffer = "helloworld";

    {
        PosixIOEngine engine;
        auto open_result = engine.open(testfile.path, Mode::Append);

        ASSERT_TRUE(open_result.has_value());
        auto& fh = open_result.value();

        /* wrote 6 bytes */
        auto append_result =
            engine.append(fh, string_buffer.data(), string_buffer.size());
        ASSERT_TRUE(append_result.has_value());
        auto val = append_result.value();
        ASSERT_EQ(val, string_buffer.size());
    }

    {
        PosixIOEngine engine;
        auto open_result = engine.open(testfile.path, Mode::Read);

        ASSERT_TRUE(open_result.has_value());
        auto& fh = open_result.value();

        std::string read_buf(5, '\0');
        auto read_result = engine.read(fh, 5, read_buf.data(), 5);
        ASSERT_TRUE(read_result.has_value());
        ASSERT_EQ("world", read_buf);
    }
}

TEST(POSIX_IO_Engine, sync_data_succeeds) {
    Tempfile testfile("tempfile-XXXXXX");
    std::string string_buffer = "helloworld";

    PosixIOEngine engine;
    auto open_result = engine.open(testfile.path, Mode::Append);

    ASSERT_TRUE(open_result.has_value());
    auto& fh = open_result.value();

    /* wrote 6 bytes */
    auto append_result =
        engine.append(fh, string_buffer.data(), string_buffer.size());
    ASSERT_TRUE(append_result.has_value());
    auto val = append_result.value();
    ASSERT_EQ(val, string_buffer.size());

    auto sync_result = engine.sync_data(fh);
    ASSERT_TRUE(sync_result.has_value());
}

TEST(POSIX_IO_Engine, sync_all_succeeds) {
    Tempfile testfile("tempfile-XXXXXX");
    std::string string_buffer = "helloworld";

    PosixIOEngine engine;
    auto open_result = engine.open(testfile.path, Mode::Append);

    ASSERT_TRUE(open_result.has_value());
    auto& fh = open_result.value();

    /* wrote 6 bytes */
    auto append_result =
        engine.append(fh, string_buffer.data(), string_buffer.size());
    ASSERT_TRUE(append_result.has_value());
    auto val = append_result.value();
    ASSERT_EQ(val, string_buffer.size());

    auto sync_result = engine.sync_all(fh);
    ASSERT_TRUE(sync_result.has_value());
}

TEST(POSIX_IO_Engine, multi_appends) {
    Tempfile testfile("tempfile-XXXXXX");
    std::string hello = "hello";
    std::string world = "world";

    {
        PosixIOEngine engine;
        auto open_result = engine.open(testfile.path, Mode::Append);

        ASSERT_TRUE(open_result.has_value());
        auto& fh = open_result.value();

        auto hello_append_result =
            engine.append(fh, hello.data(), hello.size());
        ASSERT_TRUE(hello_append_result.has_value());
        auto hval = hello_append_result.value();
        ASSERT_EQ(hval, hello.size());

        auto world_append_result =
            engine.append(fh, world.data(), world.size());
        ASSERT_TRUE(world_append_result.has_value());
        auto wval = world_append_result.value();
        ASSERT_EQ(wval, world.size());
    }

    {
        PosixIOEngine engine;
        auto open_result = engine.open(testfile.path, Mode::Read);

        ASSERT_TRUE(open_result.has_value());
        auto& fh = open_result.value();

        std::string read_buf(10, '\0');
        auto read_result = engine.read(fh, 10, read_buf.data(), 0);
        ASSERT_TRUE(read_result.has_value());
        ASSERT_EQ("helloworld", read_buf);
    }
}
