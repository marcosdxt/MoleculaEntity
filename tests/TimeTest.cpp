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
    // Data inválida tem que aparecer como ausência. A versão anterior, com
    // std::mktime, normalizava silenciosamente — mês 13 virava janeiro do ano
    // seguinte, e o registro ficava plausível e errado.
    EXPECT_FALSE(parseUtc("nao e data").has_value());
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

// O teste que guarda o defeito: a conversão não pode olhar para o fuso da
// máquina. Com std::mktime ela olhava, e o mesmo registro lido em São Paulo e
// num CI em UTC devolvia instantes diferentes — três horas de diferença que
// nenhum teste rodando em UTC jamais veria.
TEST(Time, IsIndependentOfTheMachineTimezone)
{
    const char* original = std::getenv("TZ");

    const auto emUtc = [] {
        setenv("TZ", "UTC", 1);
        tzset();
        return *parseUtc("2026-09-16 23:14:11");
    }();

    const auto emSaoPaulo = [] {
        setenv("TZ", "America/Sao_Paulo", 1);
        tzset();
        return *parseUtc("2026-09-16 23:14:11");
    }();

    const auto emTokyo = [] {
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

    EXPECT_EQ(emUtc, emSaoPaulo);
    EXPECT_EQ(emUtc, emTokyo);
}
