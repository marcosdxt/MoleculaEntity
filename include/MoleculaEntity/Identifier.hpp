#pragma once

#include <string>
#include <string_view>

namespace MoleculaEntity {

/// Aspas em nome de tabela, coluna ou índice antes de ele entrar no SQL.
///
/// Valores nunca são concatenados nesta biblioteca — vão sempre por `?`. Nomes
/// de identificador não podem ir por `?`: nenhum banco aceita parâmetro no
/// lugar de um nome. Então o que resta é citar, e é o que esta função faz.
///
/// Sem isto, um nome de coluna vindo de fora (uma ordenação escolhida na
/// interface, um filtro montado a partir de configuração) entra cru no SQL e a
/// biblioteca inteira passa a ter injeção por um caminho que ninguém olha,
/// porque "os valores estão parametrizados".
///
/// Citar também faz funcionar o que antes quebrava em silêncio: coluna chamada
/// `order`, `group` ou `index` — palavras reservadas que o SQL recusa sem aspas.
///
/// Nome qualificado é citado por parte: `pedido.total` vira `"pedido"."total"`,
/// e não `"pedido.total"`, que seria uma coluna só com um ponto no nome.
[[nodiscard]] inline std::string quoteIdentifier(std::string_view identifier)
{
    std::string out;
    out.reserve(identifier.size() + 4);

    const auto quotePart = [&out](std::string_view part) {
        out += '"';
        for (const char c : part) {
            // Aspas dentro do nome dobram — é assim que SQL escapa identificador,
            // e é o que impede fechar as aspas e continuar escrevendo SQL.
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
