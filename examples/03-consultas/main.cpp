// 03 — Consultas: o QueryBuilder inteiro, e onde ele termina.
//
// O repositório traz `find`, `count` e `exists`; o QueryBuilder é quem descreve
// o filtro. Valores vão sempre por `?` e identificadores vão sempre entre
// aspas — você não monta string de SQL em lugar nenhum aqui.

#include <MoleculaEntity/SQLite3DatabaseManager.hpp>

#include "Entities.hpp"

#include <cstdio>

using namespace MoleculaEntity;

namespace {

void mostrar(const char* titulo, const std::vector<Livraria::LivroEntity>& livros)
{
    std::printf("%s (%zu)\n", titulo, livros.size());
    for (const auto& l : livros) {
        std::printf("  %-28s %-12s R$ %6.2f  estoque %d\n",
                    l.getTitulo().c_str(), l.getCategoria().c_str(),
                    l.getPreco(), l.getEstoque());
    }
}

}  // namespace

int main()
{
    auto db = SQLite3DatabaseManager::open(":memory:");
    if (!db) { return 1; }

    Livraria::DatabaseBootstrap bootstrap(db);
    if (!bootstrap.syncAll()) {
        std::fprintf(stderr, "esquema: %s\n", bootstrap.lastError().c_str());
        return 1;
    }
    Livraria::LivroRepository livros(db);

    const struct { const char* titulo; const char* autor; const char* cat; double preco; int estoque; }
    acervo[] = {
        {"O Cortico",            "Aluisio Azevedo",  "classico",  32.90,  4},
        {"Grande Sertao",        "Guimaraes Rosa",   "classico",  79.90,  1},
        {"Vidas Secas",          "Graciliano Ramos", "classico",  41.00,  0},
        {"Clean Architecture",   "Robert Martin",    "tecnico",  145.00,  7},
        {"Domain-Driven Design", "Eric Evans",       "tecnico",  210.50,  2},
        {"Refatoracao",          "Martin Fowler",    "tecnico",  189.90,  0},
        {"O Hobbit",             "J. R. R. Tolkien", "fantasia",  54.90, 12},
    };

    for (const auto& item : acervo) {
        Livraria::LivroEntity l;
        l.setTitulo(item.titulo);
        l.setAutor(item.autor);
        l.setCategoria(item.cat);
        l.setPreco(item.preco);
        l.setEstoque(item.estoque);
        if (item.estoque == 0) { l.setEsgotadoEm("2026-09-01"); }
        livros.save(l);
    }

    // ------------------------------------------------- igualdade e faixa ---
    {
        QueryBuilder qb;
        qb.where("categoria", CompareOp::Equals, std::string{"tecnico"})
          .andWhere("preco", CompareOp::LessThan, 200.0)
          .orderBy("preco", OrderDirection::Desc);
        mostrar("técnicos abaixo de 200, do mais caro para o mais barato", livros.find(qb));
    }

    // --------------------------------------------------------- IN e LIKE ---
    {
        QueryBuilder qb;
        qb.whereIn("categoria", {std::string{"classico"}, std::string{"fantasia"}})
          .orderBy("titulo");
        mostrar("clássicos e fantasia", livros.find(qb));
    }
    {
        QueryBuilder qb;
        // O curinga é do SQL, não do construtor: você o escreve no valor.
        qb.where("autor", CompareOp::Like, std::string{"%Martin%"});
        mostrar("autor com \"Martin\"", livros.find(qb));
    }

    // ----------------------------------------------------- BETWEEN e NULL ---
    {
        QueryBuilder qb;
        qb.whereBetween("preco", 30.0, 60.0).orderBy("preco");
        mostrar("entre R$ 30 e R$ 60", livros.find(qb));
    }
    {
        QueryBuilder qb;
        qb.whereNotNull("esgotado_em");
        mostrar("esgotados (coluna preenchida)", livros.find(qb));
    }

    // ------------------------------------------------------------ AND/OR ---
    {
        QueryBuilder qb;
        // ATENÇÃO: não há parênteses. Isto é `(categoria = ? AND estoque > ?) OR
        // preco > ?`, seguindo a precedência do SQL. Para agrupar de outro jeito,
        // escreva o SQL e use db->query().
        qb.where("categoria", CompareOp::Equals, std::string{"classico"})
          .andWhere("estoque", CompareOp::GreaterThan, int64_t{0})
          .orWhere("preco", CompareOp::GreaterThan, 200.0);
        mostrar("clássicos em estoque, OU qualquer um acima de 200", livros.find(qb));
    }

    // -------------------------------------------------------- paginação ---
    for (int pagina = 0; pagina < 3; ++pagina) {
        QueryBuilder qb;
        qb.orderBy("titulo").limit(3).offset(pagina * 3);
        const auto p = livros.find(qb);
        std::printf("página %d: %zu itens", pagina + 1, p.size());
        if (!p.empty()) { std::printf(" (começa em \"%s\")", p.front().getTitulo().c_str()); }
        std::printf("\n");
    }

    // ------------------------------------------------- contar e existir ---
    {
        QueryBuilder semEstoque;
        semEstoque.where("estoque", CompareOp::Equals, int64_t{0});
        std::printf("sem estoque: %lld\n", static_cast<long long>(livros.count(semEstoque)));

        QueryBuilder caros;
        caros.where("preco", CompareOp::GreaterThan, 500.0);
        std::printf("tem algum acima de 500? %s\n", livros.exists(caros) ? "tem" : "não tem");
    }

    // ------------------------------------------------- onde ele termina ---
    // Não há JOIN no construtor. Quando a consulta passa do que ele descreve,
    // desça para o SQL — a interface do banco continua acessível, e os valores
    // continuam parametrizados.
    const auto porCategoria = db->query(
        "SELECT categoria, COUNT(*), AVG(preco) FROM livros "
        "WHERE estoque > ? GROUP BY categoria ORDER BY categoria", {int64_t{0}});

    if (!db->ok()) {
        std::fprintf(stderr, "consulta falhou: %s\n", db->lastError().c_str());
        return 1;
    }

    std::printf("resumo por categoria (só o que tem estoque):\n");
    for (const auto& linha : porCategoria) {
        std::printf("  %-10s %lld títulos, média R$ %.2f\n",
                    std::get<std::string>(linha[0]).c_str(),
                    static_cast<long long>(std::get<int64_t>(linha[1])),
                    std::get<double>(linha[2]));
    }

    return 0;
}
