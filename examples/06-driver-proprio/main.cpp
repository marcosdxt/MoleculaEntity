// 06 — Escrevendo um IDatabaseManager: rastreando cada SQL que sai.
//
// A biblioteca não fala com o SQLite: ela fala com a interface. Trocar ou
// embrulhar o que está atrás dela é o ponto de extensão — e o exemplo mais útil
// disso não é "outro banco", é um **decorador**: mesmo driver, com medição.
//
// O que sai daqui serve para responder "por que essa tela está lenta" sem
// adivinhar: quantas consultas, quais, quanto tempo cada uma.
//
// (Se você for implementar outro banco de verdade, o contrato é o mesmo: os
// mesmos onze métodos, valores por `?`, e erro sem exceção.)

#include <MoleculaEntity/MoleculaEntity.hpp>
#include <MoleculaEntity/SQLite3DatabaseManager.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace MoleculaEntity;

namespace {

class DriverComRastro final : public IDatabaseManager {
public:
    explicit DriverComRastro(DatabaseManagerPtr interno, bool imprimir = true)
        : interno_(std::move(interno)), imprimir_(imprimir) {}

    bool execute(const std::string& sql) override
    {
        return medir(sql, [&] { return interno_->execute(sql); });
    }

    bool execute(const std::string& sql, const std::vector<DbValue>& params) override
    {
        return medir(sql, [&] { return interno_->execute(sql, params); });
    }

    DbResult query(const std::string& sql) override
    {
        DbResult r;
        medir(sql, [&] { r = interno_->query(sql); return interno_->ok(); });
        return r;
    }

    DbResult query(const std::string& sql, const std::vector<DbValue>& params) override
    {
        DbResult r;
        medir(sql, [&] { r = interno_->query(sql, params); return interno_->ok(); });
        return r;
    }

    // O resto é repasse. Um decorador que "melhora" o que não precisa melhorar
    // vira um segundo driver para manter.
    int64_t lastInsertRowId() override { return interno_->lastInsertRowId(); }
    int affectedRows() override { return interno_->affectedRows(); }
    bool beginTransaction() override { return interno_->beginTransaction(); }
    bool commit() override { return interno_->commit(); }
    bool rollback() override { return interno_->rollback(); }

    [[nodiscard]] bool ok() const noexcept override { return interno_->ok(); }
    [[nodiscard]] const std::string& lastError() const noexcept override { return interno_->lastError(); }

    struct Medida {
        int chamadas = 0;
        std::chrono::microseconds total{0};
    };

    [[nodiscard]] const std::map<std::string, Medida>& medidas() const noexcept { return medidas_; }

private:
    template <typename Acao>
    bool medir(const std::string& sql, Acao&& acao)
    {
        const auto inicio = std::chrono::steady_clock::now();
        const bool ok = acao();
        const auto levou = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - inicio);

        // A chave é o SQL, não a chamada: as mesmas 200 consultas com valores
        // diferentes têm que somar numa linha só, senão o relatório é ruído.
        // Isto só funciona porque os valores vão por `?` e não dentro do texto.
        auto& m = medidas_[resumir(sql)];
        ++m.chamadas;
        m.total += levou;

        if (imprimir_) {
            std::printf("    [sql %6lld us] %s%s\n", static_cast<long long>(levou.count()),
                        resumir(sql).c_str(), ok ? "" : "  <- FALHOU");
        }
        return ok;
    }

    [[nodiscard]] static std::string resumir(const std::string& sql)
    {
        std::string uma_linha;
        uma_linha.reserve(sql.size());
        bool espaco = false;
        for (const char c : sql) {
            if (c == '\n' || c == '\t' || c == ' ') {
                espaco = true;
                continue;
            }
            if (espaco && !uma_linha.empty()) { uma_linha += ' '; }
            espaco = false;
            uma_linha += c;
        }
        return uma_linha.size() > 72 ? uma_linha.substr(0, 69) + "..." : uma_linha;
    }

    DatabaseManagerPtr interno_;
    bool imprimir_;
    std::map<std::string, Medida> medidas_;
};

class Produto : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "produtos"; }
    [[nodiscard]] int tableVersion() const override { return 1; }

    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        return {
            {"nome",  ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt},
            {"preco", ColumnType::Real, false, false, false, std::nullopt, std::nullopt, std::nullopt},
        };
    }

    [[nodiscard]] const std::string& nome() const noexcept { return nome_; }
    void setNome(std::string v) { nome_ = std::move(v); }
    [[nodiscard]] double preco() const noexcept { return preco_; }
    void setPreco(double v) noexcept { preco_ = v; }

private:
    std::string nome_;
    double preco_ = 0.0;
};

class ProdutoRepository : public BaseRepository<Produto> {
public:
    explicit ProdutoRepository(DatabaseManagerPtr db) : BaseRepository<Produto>(std::move(db)) {}

protected:
    [[nodiscard]] std::vector<std::string> getInsertColumns(const Produto&) const override
    {
        return {"id", "nome", "preco"};
    }
    [[nodiscard]] std::vector<DbValue> getInsertValues(const Produto& p) const override
    {
        return {p.getId(), p.nome(), p.preco()};
    }
    [[nodiscard]] std::vector<std::string> getUpdateColumns(const Produto&) const override
    {
        return {"nome", "preco"};
    }
    [[nodiscard]] std::vector<DbValue> getUpdateValues(const Produto& p) const override
    {
        return {p.nome(), p.preco()};
    }
    [[nodiscard]] Produto mapRowToEntity(const DbRow& row) const override
    {
        Produto p;
        p.setIdx(getInt64Value(row[0]));
        p.setId(getStringValue(row[1]));
        p.setNome(getStringValue(row[4]));
        p.setPreco(getDoubleValue(row[5]));
        return p;
    }
};

}  // namespace

int main()
{
    auto sqlite = SQLite3DatabaseManager::open(":memory:");
    if (!sqlite) { return 1; }

    // O decorador entra no lugar do driver, e mais nada muda: SchemaManager e
    // repositórios recebem um IDatabaseManager e não perguntam qual.
    auto rastro = std::make_shared<DriverComRastro>(sqlite, /*imprimir=*/false);
    DatabaseManagerPtr db = rastro;

    SchemaManager schema(db);
    if (!schema.initialize() || !schema.syncEntity<Produto>()) {
        std::fprintf(stderr, "esquema: %s\n", schema.lastError().c_str());
        return 1;
    }

    ProdutoRepository produtos(db);

    std::printf("gravando 50 produtos...\n");
    for (int i = 0; i < 50; ++i) {
        Produto p;
        p.setNome("produto " + std::to_string(i));
        p.setPreco(9.90 + i);
        produtos.save(p);
    }

    QueryBuilder caros;
    caros.where("preco", CompareOp::GreaterThan, 50.0).orderBy("preco", OrderDirection::Desc).limit(5);
    const auto top = produtos.find(caros);
    std::printf("cinco mais caros, o primeiro é %s\n", top.front().nome().c_str());

    // ------------------------------------------------------ o relatório ---
    std::printf("\nSQL por forma, do mais caro para o mais barato:\n");

    std::vector<std::pair<std::string, DriverComRastro::Medida>> linhas(
        rastro->medidas().begin(), rastro->medidas().end());
    std::sort(linhas.begin(), linhas.end(),
              [](const auto& a, const auto& b) { return a.second.total > b.second.total; });

    for (const auto& [sql, m] : linhas) {
        std::printf("  %5d x %7lld us  %s\n", m.chamadas,
                    static_cast<long long>(m.total.count()), sql.c_str());
    }

    // O `save()` faz INSERT e volta a ler a linha para devolvê-la como ficou —
    // por isso o SELECT aparece 50 vezes aqui. É o tipo de coisa que só se vê
    // medindo, e é exatamente para isso que este decorador serve.
    return 0;
}
