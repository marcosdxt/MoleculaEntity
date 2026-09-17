#pragma once

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace MoleculaEntity {

/// Conversão entre o texto que o banco guarda e `system_clock::time_point`.
///
/// **Tudo aqui é UTC, e não é preferência estética.** O `CURRENT_TIMESTAMP` do
/// SQLite — que é o default das colunas `created_at`/`updated_at` e o que o
/// gatilho de `updated_at` grava — produz UTC. Ler aquele texto com `mktime`,
/// que interpreta hora LOCAL, desloca todo carimbo pelo fuso da máquina: num
/// host em UTC−3 o registro volta três horas no passado, silenciosamente, e
/// nenhum teste que roda em UTC percebe.
///
/// As funções abaixo não consultam o fuso do sistema em momento nenhum.
namespace time_utils {

/// Dias desde 1970-01-01 para uma data do calendário civil, sem depender de
/// `timegm` (que não existe em toda plataforma) nem de `mktime` (que é local).
/// Algoritmo de Howard Hinnant, `days_from_civil`.
[[nodiscard]] inline int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) noexcept
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153U * (m + (m > 2 ? -3U : 9U)) + 2U) / 5U + d - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

/// A inversa: data civil a partir dos dias desde a epoch.
inline void civilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d) noexcept
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    const int64_t yr = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    const unsigned mp = (5U * doy + 2U) / 153U;
    d = doy - (153U * mp + 2U) / 5U + 1U;
    m = mp + (mp < 10U ? 3U : -9U);
    y = yr + (m <= 2U);
}

/// Aceita o que o SQLite escreve e o que gente escreve à mão:
///
///     2026-09-16 23:14:11        (o formato do CURRENT_TIMESTAMP)
///     2026-09-16T23:14:11Z       (ISO 8601)
///     2026-09-16T23:14:11.250Z   (com fração — truncada para o segundo)
///     2026-09-16                 (meia-noite UTC)
///
/// Devolve vazio quando o texto não é nenhum desses: data inválida tem que ser
/// visível como ausência, nunca como um instante plausível e errado.
[[nodiscard]] inline std::optional<std::chrono::system_clock::time_point>
parseUtc(std::string_view text)
{
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;

    const std::string buffer(text);
    int consumed = 0;
    const int fields = std::sscanf(buffer.c_str(), "%4d-%2d-%2d%n", &year, &month, &day, &consumed);
    if (fields != 3) {
        return std::nullopt;
    }

    if (static_cast<std::size_t>(consumed) < buffer.size()) {
        const char separator = buffer[static_cast<std::size_t>(consumed)];
        if (separator != ' ' && separator != 'T' && separator != 't') {
            return std::nullopt;
        }
        if (std::sscanf(buffer.c_str() + consumed + 1, "%2d:%2d:%2d", &hour, &minute, &second) != 3) {
            return std::nullopt;
        }
    }

    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) {
        return std::nullopt;
    }

    const int64_t days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    const int64_t seconds = days * 86400 + hour * 3600 + minute * 60 + second;

    return std::chrono::system_clock::time_point{std::chrono::seconds{seconds}};
}

/// O formato que o SQLite usa no `CURRENT_TIMESTAMP`, em UTC — para quem
/// precisa gravar um carimbo à mão e quer que ele compare com os automáticos.
[[nodiscard]] inline std::string formatUtc(std::chrono::system_clock::time_point tp)
{
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();

    int64_t days = seconds / 86400;
    int64_t rest = seconds % 86400;
    if (rest < 0) {          // instantes anteriores a 1970 arredondam para baixo
        rest += 86400;
        --days;
    }

    int64_t year = 0;
    unsigned month = 0, day = 0;
    civilFromDays(days, year, month, day);

    // 64 e não 32: o ano é int64_t e o compilador não tem como provar que cabe.
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%04lld-%02u-%02u %02lld:%02lld:%02lld",
                  static_cast<long long>(year), month, day,
                  static_cast<long long>(rest / 3600),
                  static_cast<long long>((rest % 3600) / 60),
                  static_cast<long long>(rest % 60));
    return buffer;
}

}  // namespace time_utils
}  // namespace MoleculaEntity
