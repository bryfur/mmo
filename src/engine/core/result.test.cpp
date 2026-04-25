#include <gtest/gtest.h>

#include "engine/core/result.hpp"

#include <string>

using namespace mmo::engine::core;

TEST(Result, OkPath) {
    Result<int> r = 42;
    EXPECT_TRUE(r.is_ok());
    EXPECT_FALSE(r.is_err());
    EXPECT_EQ(r.value(), 42);
}

TEST(Result, ErrPath) {
    Result<int> r = Err<std::string>{"boom"};
    EXPECT_TRUE(r.is_err());
    EXPECT_FALSE(r.is_ok());
    EXPECT_EQ(r.error(), "boom");
}

TEST(Result, ValueOrFallback) {
    Result<int> ok = 7;
    Result<int> err = Err<std::string>{"no"};
    EXPECT_EQ(ok.value_or(0), 7);
    EXPECT_EQ(err.value_or(99), 99);
}

TEST(Result, MapTransformsOk) {
    Result<int> r = 3;
    auto s = r.map([](int v) { return v * 2; });
    EXPECT_TRUE(s.is_ok());
    EXPECT_EQ(s.value(), 6);
}

TEST(Result, MapPropagatesErr) {
    Result<int> r = Err<std::string>{"fail"};
    auto s = r.map([](int v) { return v * 2; });
    EXPECT_TRUE(s.is_err());
    EXPECT_EQ(s.error(), "fail");
}

TEST(Result, AndThenChainsOk) {
    Result<int> r = 5;
    auto s = r.and_then([](int v) -> Result<int> { return v + 10; });
    EXPECT_TRUE(s.is_ok());
    EXPECT_EQ(s.value(), 15);
}

TEST(Result, AndThenShortCircuitsOnErr) {
    Result<int> r = Err<std::string>{"stop"};
    auto s = r.and_then([](int v) -> Result<int> { return v + 10; });
    EXPECT_TRUE(s.is_err());
    EXPECT_EQ(s.error(), "stop");
}

TEST(Result, AndThenCanReturnError) {
    Result<int> r = 5;
    auto s = r.and_then([](int) -> Result<int> { return Err<std::string>{"bad"}; });
    EXPECT_TRUE(s.is_err());
    EXPECT_EQ(s.error(), "bad");
}

TEST(Result, OkFactory) {
    auto r = Ok(123);
    EXPECT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 123);
}

TEST(Result, BoolConversion) {
    Result<int> ok = 1;
    Result<int> err = Err<std::string>{"x"};
    EXPECT_TRUE(static_cast<bool>(ok));
    EXPECT_FALSE(static_cast<bool>(err));
}
