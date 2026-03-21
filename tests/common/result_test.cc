#include "enigmadb/common/result.h"
#include "gtest/gtest.h"

TEST(Result, result_expectOk) {
  auto res = ExpectResult<int, nullptr_t>::ok(1);
  ASSERT_TRUE(res.has_value());

  auto value = res.value();
  ASSERT_EQ(value, 1);
}

TEST(Result, result_expectErr) {
  const std::string err_msg = "failed with a runtime error";
  auto res = ExpectResult<nullptr_t, std::runtime_error>::err(
      std::runtime_error(err_msg));
  ASSERT_FALSE(res.has_value());

  auto &error = res.err();
  EXPECT_STREQ(error.what(), err_msg.c_str());
}

TEST(Result, result_expectErrInvalidAccessToData) {
  const std::string expected_err_msg = "invalid access to data";
  const std::string err_msg = "failed with a runtime error";
  auto res = ExpectResult<nullptr_t, std::runtime_error>::err(
      std::runtime_error(err_msg));
  ASSERT_FALSE(res.has_value());

  try {
    res.value();
  } catch (std::exception &e) {
    EXPECT_STREQ(e.what(), expected_err_msg.c_str());
  }
}
