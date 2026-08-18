#include "enigmadb/result.h"

#include "enigmadb/error.h"
#include "gtest/gtest.h"

using namespace enigmadb;

TEST(Result, result_expectOk) {
    auto res = ExpectResult<int, std::runtime_error>::ok(1);
    ASSERT_TRUE(res.has_value());

    auto value = res.value();
    ASSERT_EQ(value, 1);
}

TEST(Result, result_expectOkInvalidAccessToError) {
    const std::string expected_err_msg = "invalid access to error";
    auto res = ExpectResult<int, std::runtime_error>::ok(1);
    ASSERT_TRUE(res.has_value());

    try {
        res.error();
        FAIL();
    } catch (std::exception& e) {
        EXPECT_STREQ(e.what(), expected_err_msg.c_str());
    }
}

TEST(Result, result_expectOkMoveSemantics) {
    auto res = ExpectResult<int, std::runtime_error>::ok(1);
    ASSERT_TRUE(res.has_value());

    auto resmoved = std::move(res);
    ASSERT_TRUE(resmoved.has_value());

    auto value = resmoved.value();
    ASSERT_EQ(value, 1);
}

TEST(Result, result_expectOkWithVoid) {
    auto res = ExpectResult<void, Error>::ok();
    ASSERT_TRUE(res.has_value());
}

TEST(Result, result_expectErr) {
    const std::string err_msg = "failed with a runtime error";
    auto res = ExpectResult<int, std::runtime_error>::err(std::runtime_error(err_msg));
    ASSERT_FALSE(res.has_value());

    auto& error = res.error();
    EXPECT_STREQ(error.what(), err_msg.c_str());
}

TEST(Result, result_expectErrInvalidAccessToData) {
    const std::string expected_err_msg = "invalid access to data";
    const std::string err_msg = "failed with a runtime error";
    auto res = ExpectResult<int, std::runtime_error>::err(std::runtime_error(err_msg));
    ASSERT_FALSE(res.has_value());

    try {
        res.value();
    } catch (std::exception& e) {
        EXPECT_STREQ(e.what(), expected_err_msg.c_str());
    }
}

TEST(Result, result_expectErrMoveSemantics) {
    const std::string err_msg = "failed with a runtime error";
    auto res = ExpectResult<int, std::runtime_error>::err(std::runtime_error(err_msg));
    ASSERT_FALSE(res.has_value());

    auto& error = res.error();
    EXPECT_STREQ(error.what(), err_msg.c_str());

    auto resmoved = std::move(res);
    ASSERT_FALSE(resmoved.has_value());

    auto& errormoved = resmoved.error();
    EXPECT_STREQ(errormoved.what(), err_msg.c_str());
}

TEST(Result, result_expectErrWithCustomError) {
    const std::string err_msg = "failed with an unexpected error";
    auto res = ExpectResult<std::string, Error>::err(Error::unexpected(err_msg));
    ASSERT_FALSE(res.has_value());

    auto& error = res.error();
    EXPECT_STREQ(error.message.c_str(), err_msg.c_str());
}

TEST(Result, result_expectErrWithVoid) {
    const std::string err_msg = "failed with an unexpected error";
    auto res = ExpectResult<void, Error>::err(Error::unexpected(err_msg));
    ASSERT_FALSE(res.has_value());

    auto& error = res.error();
    EXPECT_STREQ(error.message.c_str(), err_msg.c_str());
}
