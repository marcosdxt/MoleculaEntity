// 01 — O básico, sem gerador nenhum.
//
// Uma entidade, um repositório, e o ciclo inteiro: criar, ler, alterar, apagar.
// Tudo escrito à mão, de propósito: é este o contrato que a biblioteca pede, e
// é ele que o gerador do exemplo 02 preenche por você.
//
//     cmake --build build --target exemplo-01 && ./build/examples/exemplo-01

#include <MoleculaEntity/MoleculaEntity.hpp>
#include <MoleculaEntity/SQLite3DatabaseManager.hpp>

#include <cstdio>
#include <string>

using namespace MoleculaEntity;

// ---------------------------------------------------------------- entidade ---
// `idx`, `id`, `created_at` e `updated_at` vêm do BaseEntity e não se declaram
// aqui — ver "Colunas base" no README.
//
// Repare no que NÃO está escrito: construtor de cópia, de movimento, operadores
// de atribuição. O BaseEntity tem destrutor virtual, e enquanto a derivada não
// declarar o dela, o compilador gera os quatro. Declarar `~Tarefa() = default;`
// por hábito suprimiria o construtor de movimento.
class Tarefa : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "tarefas"; }
    [[nodiscard]] int tableVersion() const override { return 1; }

    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        // {nome, tipo, aceita nulo, chave primária, único, default, fk tabela, fk coluna}
        return {
            {"titulo",    ColumnType::Text,    false, false, false, std::nullopt, std::nullopt, std::nullopt},
            {"concluida", ColumnType::Boolean, false, false, false, "0",          std::nullopt, std::nullopt},
            {"prazo",     ColumnType::Text,    true,  false, false, std::nullopt, std::nullopt, std::nullopt},
        };
    }

    [[nodiscard]] const std::string& titulo() const noexcept { return titulo_; }
    void setTitulo(std::string v) { titulo_ = std::move(v); }

    [[nodiscard]] bool concluida() const noexcept { return concluida_; }
    void setConcluida(bool v) noexcept { concluida_ = v; }

    [[nodiscard]] const std::optional<std::string>& prazo() const noexcept { return prazo_; }
    void setPrazo(std::optional<std::string> v) { prazo_ = std::move(v); }

private:
    std::string titulo_;
    bool concluida_ = false;
    std::optional<std::string> prazo_;
};

// -------------------------------------------------------------- repositório ---
// As cinco funções abaixo são a ponte entre a entidade e as linhas do banco. É
// o único lugar onde os nomes das colunas e a ordem dos valores precisam
// concordar — e é exatamente por isso que existe um gerador (exemplo 02): essa
// concordância é fácil de quebrar numa alteração de três meses depois.
class TarefaRepository : public BaseRepository<Tarefa> {
public:
    explicit TarefaRepository(DatabaseManagerPtr db) : BaseRepository<Tarefa>(std::move(db)) {}

    [[nodiscard]] std::vector<Tarefa> pendentes()
    {
        QueryBuilder qb;
        qb.where("concluida", CompareOp::Equals, int64_t{0})
          .orderBy("prazo");
        return find(qb);
    }

protected:
    [[nodiscard]] std::vector<std::string> getInsertColumns(const Tarefa&) const override
    {
        return {"id", "titulo", "concluida", "prazo"};
    }

    [[nodiscard]] std::vector<DbValue> getInsertValues(const Tarefa& t) const override
    {
        return {t.getId(), t.titulo(), int64_t{t.concluida()},
                t.prazo().has_value() ? DbValue{*t.prazo()} : DbValue{nullptr}};
    }

    // Sem `id`: o uuid é identidade, não dado. Alterá-lo num UPDATE faria a
    // linha deixar de ser a mesma coisa para quem a referencia de fora.
    [[nodiscard]] std::vector<std::string> getUpdateColumns(const Tarefa&) const override
    {
        return {"titulo", "concluida", "prazo"};
    }

    [[nodiscard]] std::vector<DbValue> getUpdateValues(const Tarefa& t) const override
    {
        return {t.titulo(), int64_t{t.concluida()},
                t.prazo().has_value() ? DbValue{*t.prazo()} : DbValue{nullptr}};
    }

    // A ordem das colunas aqui é a do `SELECT *`, que segue o CREATE TABLE:
    // primeiro as quatro colunas base, depois as suas.
    [[nodiscard]] Tarefa mapRowToEntity(const DbRow& row) const override
    {
        Tarefa t;
        t.setIdx(getInt64Value(row[0]));
        t.setId(getStringValue(row[1]));
        if (auto ts = parseTimestamp(row[2])) { t.setCreatedAt(*ts); }
        if (auto ts = parseTimestamp(row[3])) { t.setUpdatedAt(*ts); }
        t.setTitulo(getStringValue(row[4]));
        t.setConcluida(getInt64Value(row[5]) != 0);
        t.setPrazo(isNull(row[6]) ? std::nullopt : std::optional{getStringValue(row[6])});
        return t;
    }
};

int main()
{
    // `:memory:` para o exemplo rodar sem deixar arquivo. Em produção seria um
    // caminho, e aí as opções do driver passam a importar — ver o exemplo 05.
    auto db = SQLite3DatabaseManager::open(":memory:");
    if (!db) {
        std::fprintf(stderr, "não abriu o banco\n");
        return 1;
    }

    // Os dois devolvem `bool`, e o retorno é [[nodiscard]]: esquema que não
    // subiu não é detalhe — é tudo o que vem depois rodando contra uma tabela
    // que não existe, ou que está no formato errado.
    SchemaManager schema(db);
    if (!schema.initialize() || !schema.syncEntity<Tarefa>()) {
        std::fprintf(stderr, "esquema: %s\n", schema.lastError().c_str());
        return 1;
    }

    TarefaRepository tarefas(db);

    // ------------------------------------------------------------- criar ---
    Tarefa comprar;
    comprar.setTitulo("Comprar café");
    comprar.setPrazo("2026-09-20");
    comprar = tarefas.save(comprar);  // o save devolve a linha como ela ficou

    Tarefa revisar;
    revisar.setTitulo("Revisar o PR");
    revisar = tarefas.save(revisar);

    std::printf("criadas: idx=%lld e idx=%lld\n",
                static_cast<long long>(*comprar.getIdx()),
                static_cast<long long>(*revisar.getIdx()));
    std::printf("o uuid saiu pronto do save: %s\n", comprar.getId().c_str());

    // -------------------------------------------------------------- ler ---
    for (const auto& t : tarefas.pendentes()) {
        // Sem %-14s: a largura do printf conta BYTES, e "café" tem cinco
        // bytes para quatro letras — a coluna sairia torta.
        std::printf("  [ ] %s (prazo: %s)\n", t.titulo().c_str(),
                    t.prazo() ? t.prazo()->c_str() : "sem prazo");
    }

    // ----------------------------------------------------------- alterar ---
    comprar.setConcluida(true);
    comprar = tarefas.save(comprar);  // tem idx, então o save vira UPDATE

    std::printf("pendentes agora: %lld\n", static_cast<long long>(tarefas.pendentes().size()));

    // ------------------------------------------------------------ apagar ---
    tarefas.remove(revisar);
    std::printf("total no fim: %lld\n", static_cast<long long>(tarefas.count()));

    return 0;
}
