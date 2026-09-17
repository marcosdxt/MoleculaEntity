// 04 — Migrações: o banco que já existe no campo.
//
// Criar tabela em banco vazio é fácil. O caso que dói é o outro: a versão 1 do
// seu programa está instalada, tem dados dentro, e a versão 2 precisa de uma
// coluna nova sem perder nada.
//
// Este exemplo simula exatamente isso, num arquivo de verdade (não `:memory:`,
// senão não haveria "banco que já existe"): abre, escreve, fecha, e reabre com
// a entidade na versão seguinte.

#include <MoleculaEntity/MoleculaEntity.hpp>
#include <MoleculaEntity/SQLite3DatabaseManager.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace MoleculaEntity;

namespace {

// ------------------------------------------------------- a versão 1 dela ---
class NotaV1 : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "notas"; }
    [[nodiscard]] int tableVersion() const override { return 1; }

    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        return {
            {"texto", ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt},
        };
    }
};

// --------------------------------- a versão 2, um release depois ------------
// Duas coisas mudam juntas, e as duas são obrigatórias:
//
//   1. `columns()` passa a descrever a tabela COMO ELA DEVE SER hoje — é o que
//      vale para quem instala do zero.
//   2. `migrations()` diz como sair da 1 para a 2 — é o que vale para quem já
//      tem a tabela antiga com dados dentro.
//
// Esquecer a (1) faz a instalação nova nascer sem a coluna; esquecer a (2)
// quebra quem atualiza. O `version` amarra as duas.
class NotaV2 : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "notas"; }
    [[nodiscard]] int tableVersion() const override { return 2; }

    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        return {
            {"texto",    ColumnType::Text,    false, false, false, std::nullopt,  std::nullopt, std::nullopt},
            {"arquivada", ColumnType::Boolean, false, false, false, "0",          std::nullopt, std::nullopt},
        };
    }

    [[nodiscard]] std::vector<Migration> migrations() const override
    {
        return {
            {2, "coluna arquivada",
             "ALTER TABLE notas ADD COLUMN arquivada INTEGER NOT NULL DEFAULT 0",
             "-- SQLite antigo não sabe DROP COLUMN; a volta seria recriar a tabela"},
        };
    }
};

void mostrarColunas(const DatabaseManagerPtr& db)
{
    const auto info = db->query("PRAGMA table_info(notas)");
    std::printf("  colunas:");
    for (const auto& linha : info) {
        std::printf(" %s", std::get<std::string>(linha[1]).c_str());
    }
    std::printf("\n");
}

void mostrarVersoes(const DatabaseManagerPtr& db)
{
    const auto linhas = db->query(
        "SELECT table_name, version, description FROM __schema_migrations ORDER BY version");
    for (const auto& l : linhas) {
        std::printf("  aplicada: %s v%lld — %s\n",
                    std::get<std::string>(l[0]).c_str(),
                    static_cast<long long>(std::get<int64_t>(l[1])),
                    std::get<std::string>(l[2]).c_str());
    }
}

}  // namespace

int main()
{
    const std::string caminho = "exemplo-04-migracoes.db";
    std::remove(caminho.c_str());
    std::remove((caminho + "-wal").c_str());
    std::remove((caminho + "-shm").c_str());

    // ------------------------------------------- release 1, no campo ---
    {
        auto db = SQLite3DatabaseManager::open(caminho);
        if (!db) { return 1; }

        SchemaManager schema(db);
        schema.initialize();
        schema.syncEntity<NotaV1>();

        db->execute("INSERT INTO notas (id, texto) VALUES (?, ?)",
                    {std::string{"nota-1"}, std::string{"comprar pão"}});
        db->execute("INSERT INTO notas (id, texto) VALUES (?, ?)",
                    {std::string{"nota-2"}, std::string{"ligar para o cartório"}});

        std::printf("release 1 instalado\n");
        mostrarColunas(db);
    }

    // ---------------------------------- release 2, atualização no campo ---
    {
        auto db = SQLite3DatabaseManager::open(caminho);
        if (!db) { return 1; }

        SchemaManager schema(db);
        schema.initialize();

        // Mesma chamada de sempre. Ela cria a tabela quando não existe e aplica
        // as migrações que faltam quando existe — tudo numa transação: se o
        // `ALTER TABLE` falhar no meio, nada fica pela metade.
        schema.syncEntity<NotaV2>();

        std::printf("release 2 aplicado\n");
        mostrarColunas(db);
        mostrarVersoes(db);

        const auto linhas = db->query("SELECT texto, arquivada FROM notas ORDER BY idx");
        std::printf("  os dados antigos continuam lá:\n");
        for (const auto& l : linhas) {
            std::printf("    %s (arquivada=%lld)\n",
                        std::get<std::string>(l[0]).c_str(),
                        static_cast<long long>(std::get<int64_t>(l[1])));
        }
    }

    // ------------------------------- rodar de novo não pode fazer nada ---
    // Idempotência importa: o programa sobe muitas vezes, e o sync roda em todas.
    {
        auto db = SQLite3DatabaseManager::open(caminho);
        SchemaManager schema(db);
        schema.initialize();
        schema.syncEntity<NotaV2>();

        const auto aplicadas = db->query("SELECT COUNT(*) FROM __schema_migrations");
        const auto total = std::get<int64_t>(aplicadas[0][0]);
        std::printf("terceira subida: %lld migraç%s registrada%s — não duplicou\n",
                    static_cast<long long>(total), total == 1 ? "ão" : "ões", total == 1 ? "" : "s");
    }

    std::remove(caminho.c_str());
    std::remove((caminho + "-wal").c_str());
    std::remove((caminho + "-shm").c_str());
    return 0;
}
