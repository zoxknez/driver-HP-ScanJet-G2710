#include "util/Result.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

using namespace g2710;

TEST(Result, HoldsValue) {
    Result<int> r{42};
    ASSERT_TRUE(r.hasValue());
    EXPECT_TRUE(static_cast<bool>(r));
    EXPECT_EQ(r.value(), 42);
    EXPECT_EQ(r.code(), ErrorCode::Ok);
}

TEST(Result, HoldsError) {
    Result<int> r{fail(ErrorCode::Timeout, "bulkRead", 121)};
    ASSERT_FALSE(r.hasValue());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.code(), ErrorCode::Timeout);
    EXPECT_EQ(r.error().win32, 121u);
    EXPECT_STREQ(r.error().context, "bulkRead");
}

TEST(Result, ValueOrFallsBack) {
    Result<int> good{7};
    Result<int> bad{fail(ErrorCode::Internal, "x")};
    EXPECT_EQ(good.valueOr(-1), 7);
    EXPECT_EQ(bad.valueOr(-1), -1);
}

TEST(Result, MovesNonCopyablePayload) {
    Result<std::unique_ptr<std::string>> r{std::make_unique<std::string>("scan")};
    ASSERT_TRUE(r.hasValue());

    auto moved = std::move(r).value();
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(*moved, "scan");
}

TEST(Result, CopyPreservesValue) {
    Result<std::string> original{std::string("flatbed")};
    Result<std::string> copy = original;
    ASSERT_TRUE(copy.hasValue());
    EXPECT_EQ(copy.value(), "flatbed");
    EXPECT_EQ(original.value(), "flatbed");
}

TEST(Result, DestroysPayloadExactlyOnce) {
    struct Counter {
        static int& live() {
            static int n = 0;
            return n;
        }
        Counter() { ++live(); }
        Counter(const Counter&) { ++live(); }
        Counter(Counter&&) noexcept { ++live(); }
        ~Counter() { --live(); }
    };

    Counter::live() = 0;
    {
        Result<Counter> r{Counter{}};
        Result<Counter> moved = std::move(r);
        EXPECT_GT(Counter::live(), 0);
    }
    EXPECT_EQ(Counter::live(), 0);
}

TEST(StatusVoid, OkAndFail) {
    const Status good = ok();
    EXPECT_TRUE(good.hasValue());
    EXPECT_EQ(good.code(), ErrorCode::Ok);

    const Status bad = fail(ErrorCode::TransportLost, "controlIn");
    EXPECT_FALSE(bad.hasValue());
    EXPECT_EQ(bad.code(), ErrorCode::TransportLost);
}

TEST(ErrorCodeNames, EveryCodeHasAName) {
    // Ako se doda kod a zaboravi ime, dijagnostika kod prijatelja postaje
    // beskorisna - zato je ovo test, ne dokumentacija.
    const ErrorCode all[] = {
        ErrorCode::Ok, ErrorCode::NotOpen, ErrorCode::Timeout,
        ErrorCode::ShortTransfer, ErrorCode::Stalled, ErrorCode::Cancelled,
        ErrorCode::TransportLost, ErrorCode::DeviceNotFound, ErrorCode::DeviceError,
        ErrorCode::Busy, ErrorCode::SafetyViolation, ErrorCode::NotImplementedIn10,
        ErrorCode::InvalidArgument, ErrorCode::InvalidState, ErrorCode::Internal,
    };
    for (const ErrorCode code : all) {
        EXPECT_STRNE(toString(code), "Unknown");
    }
}
