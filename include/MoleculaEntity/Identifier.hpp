#pragma once

#include <string>
#include <string_view>

namespace MoleculaEntity {

/// Quotes a table, column or index name before it goes into the SQL.
///
/// Values are never concatenated in this library — they always travel as `?`.
/// Identifier names cannot travel as `?`: no database accepts a parameter in
/// place of a name. So what's left is quoting, and that's what this does.
///
/// Without it, a column name coming from outside (an ordering chosen in the UI,
/// a filter assembled from configuration) goes straight into the SQL, and the
/// whole library has an injection path nobody looks at, because "the values are
/// parameterized".
///
/// Quoting also fixes something that used to break quietly: a column named
/// `order`, `group` or `index` — reserved words that SQL refuses unqualified.
///
/// A qualified name is quoted part by part: `order.total` becomes
/// `"order"."total"`, not `"order.total"`, which would be a single column with a
/// dot in its name.
[[nodiscard]] inline std::string quoteIdentifier(std::string_view identifier)
{
    std::string out;
    out.reserve(identifier.size() + 4);

    const auto quotePart = [&out](std::string_view part) {
        out += '"';
        for (const char c : part) {
            // A quote inside the name is doubled — that's how SQL escapes an
            // identifier, and it's what stops anyone from closing the quotes and
            // carrying on writing SQL.
            if (c == '"') {
                out += '"';
            }
            out += c;
        }
        out += '"';
    };

    std::size_t start = 0;
    while (true) {
        const std::size_t dot = identifier.find('.', start);
        if (dot == std::string_view::npos) {
            quotePart(identifier.substr(start));
            break;
        }
        quotePart(identifier.substr(start, dot - start));
        out += '.';
        start = dot + 1;
    }

    return out;
}

}  // namespace MoleculaEntity
