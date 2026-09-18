#include <gtest/gtest.h>

#include <MoleculaEntity/Time.hpp>

#include <cstdlib>

using namespace MoleculaEntity::time_utils;

TEST(Time, ParsesTheFormatSqliteWrites)
{
    const auto tp = parseUtc("2026-09-16 23:14:11");
    ASSERT_TRUE(tp.has_value());
    EXPECT_EQ(formatUtc(*tp), "2026-09-16 23:14:11");
}

TEST(Time, ParsesIso8601WithFractionAndZ)
{
    const auto tp = parseUtc("2026-09-16T23:14:11.250Z");
    ASSERT_TRUE(tp.has_value());
    EXPECT_EQ(formatUtc(*tp), "2026-09-16 23:14:11");
}

TEST(Time, ParsesDateOnlyAsMidnight)
{
    const auto tp = parseUtc("2026-09-16");
    ASSERT_TRUE(tp.has_value());
    EXPECT_EQ(formatUtc(*tp), "2026-09-16 00:00:00");
}

TEST(Time, RejectsGarbageInsteadOfGuessing)
{
    // An invalid date has to show up as absence. The earlier version, using
    // std::mktime, normalized silently — month 13 became January of the next
    // year, and the record ended up plausible and wrong.
    EXPECT_FALSE(parseUtc("not a date").has_value());
    EXPECT_FALSE(parseUtc("2026-13-01 00:00:00").has_value());
    EXPECT_FALSE(parseUtc("").has_value());
}

TEST(Time, EpochAndBeforeIt)
{
    EXPECT_EQ(formatUtc(std::chrono::system_clock::time_point{}), "1970-01-01 00:00:00");
    EXPECT_EQ(formatUtc(std::chrono::system_clock::time_point{std::chrono::seconds{-1}}),
              "1969-12-31 23:59:59");
}

TEST(Time, LeapDay)
{
    const auto tp = parseUtc("2024-02-29 12:00:00");
    ASSERT_TRUE(tp.has_value());
    EXPECT_EQ(formatUtc(*tp), "2024-02-29 12:00:00");
}

// The test that guards the defect: the conversion must not look at the machine's
// timezone. With std::mktime it did, and the same record read in Sao Paulo and on
// a CI in UTC produced different instants — three hours apart, which no test
// running in UTC would ever see.
TEST(Time, IsIndependentOfTheMachineTimezone)
{
    const char* original = std::getenv("TZ");

    const auto inUtc = [] {
        setenv("TZ", "UTC", 1);
        tzset();
        return *parseUtc("2026-09-16 23:14:11");
    }();

    const auto inSaoPaulo = [] {
        setenv("TZ", "America/Sao_Paulo", 1);
        tzset();
        return *parseUtc("2026-09-16 23:14:11");
    }();

    const auto inTokyo = [] {
        setenv("TZ", "Asia/Tokyo", 1);
        tzset();
        return *parseUtc("2026-09-16 23:14:11");
    }();

    if (original != nullptr) {
        setenv("TZ", original, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();

    EXPECT_EQ(inUtc, inSaoPaulo);
    EXPECT_EQ(inUtc, inTokyo);
}
