#include <gtest/gtest.h>

#include <MoleculaEntity/MoleculaEntity.hpp>
#include <MoleculaEntity/SQLite3DatabaseManager.hpp>

#include <string>

using namespace MoleculaEntity;

namespace {

class NotaV1 : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "notas"; }
    [[nodiscard]] int tableVersion() const override { return 1; }
    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        return {{"texto", ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt}};
    }
};

// A migração 2 é inválida de propósito. O que se cobra dela não é falhar — é
// falhar SEM deixar rastro: nada aplicado, nada registrado, e a próxima subida
// tentando de novo.
class NotaComMigracaoQuebrada : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "notas"; }
    [[nodiscard]] int tableVersion() const override { return 2; }
    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        return {{"texto", ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt},
                {"autor", ColumnType::Text, true, false, false, std::nullopt, std::nullopt, std::nullopt}};
    }
    [[nodiscard]] std::vector<Migration> migrations() const override
    {
        return {{2, "autor", "ISTO NAO E SQL VALIDO", ""}};
    }
};

// Duas etapas: a primeira funciona, a segunda não. É o caso que separa
// "verificou o retorno" de "fez transação de verdade" — sem rollback, a coluna
// da etapa 1 fica no banco.
class NotaComSegundaEtapaQuebrada : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "notas"; }
    [[nodiscard]] int tableVersion() const override { return 3; }
    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        return {{"texto", ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt}};
    }
    [[nodiscard]] std::vector<Migration> migrations() const override
    {
        return {{2, "coluna boa", "ALTER TABLE notas ADD COLUMN autor TEXT", ""},
                {3, "coluna ruim", "ALTER TABLE notas ADD COLUMN autor TEXT", ""}};  // repetida: erro
    }
};

// Versão declarada sem a migração que chega nela. O caso não é exótico: é o que
// acontece quando alguém sobe o `version` e esquece de escrever o `migrations()`.
class NotaComVersaoSemMigracao : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "notas"; }
    [[nodiscard]] int tableVersion() const override { return 3; }
    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        return {{"texto", ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt}};
    }
    // migrations() vazio de propósito.
};

class MigrationFailureTest : public ::testing::Test {
protected:
    std::shared_ptr<SQLite3DatabaseManager> db;
    std::unique_ptr<SchemaManager> schema;

    void SetUp() override
    {
        db = SQLite3DatabaseManager::open(":memory:");
        ASSERT_TRUE(db);
        schema = std::make_unique<SchemaManager>(db);
        ASSERT_TRUE(schema->initialize());
        ASSERT_TRUE(schema->syncEntity<NotaV1>());

        ASSERT_TRUE(db->execute("INSERT INTO notas (id, texto) VALUES (?, ?)",
                                {std::string{"n1"}, std::string{"a primeira nota"}}));
    }

    [[nodiscard]] int versaoRegistrada()
    {
        const auto r = db->query("SELECT MAX(version) FROM __schema_migrations WHERE table_name = ?",
                                 {std::string{"notas"}});
        if (r.empty() || std::holds_alternative<std::nullptr_t>(r[0][0])) { return 0; }
        return static_cast<int>(std::get<int64_t>(r[0][0]));
    }

    [[nodiscard]] bool temColuna(const std::string& nome)
    {
        for (const auto& linha : db->query("PRAGMA table_info(notas)")) {
            if (std::get<std::string>(linha[1]) == nome) { return true; }
        }
        return false;
    }
};

}  // namespace

// O defeito: o driver reporta falha devolvendo `false`, e o runMigrations só
// tratava exceção. O retorno era ignorado, então a versão era REGISTRADA e a
// transação CONFIRMADA mesmo com a migração falhando — e a próxima subida via
// versão 2 no banco e nunca mais tentava. A coluna nunca chegaria, em silêncio.
TEST_F(MigrationFailureTest, FailedMigrationIsNotRecorded)
{
    EXPECT_FALSE(schema->syncEntity<NotaComMigracaoQuebrada>());

    EXPECT_EQ(versaoRegistrada(), 1) << "versão de migração que falhou não pode ser registrada";
    EXPECT_FALSE(temColuna("autor"));
}

TEST_F(MigrationFailureTest, FailureInTheSecondStepRollsBackTheFirst)
{
    EXPECT_FALSE(schema->syncEntity<NotaComSegundaEtapaQuebrada>());

    // A etapa 2 criou a coluna e a 3 falhou: sem transação de verdade, "autor"
    // ficaria no banco com a versão registrada em 2.
    EXPECT_EQ(versaoRegistrada(), 1);
    EXPECT_FALSE(temColuna("autor")) << "a etapa que deu certo tinha que voltar junto";
}

TEST_F(MigrationFailureTest, DataSurvivesAFailedMigration)
{
    EXPECT_FALSE(schema->syncEntity<NotaComMigracaoQuebrada>());

    const auto linhas = db->query("SELECT texto FROM notas");
    ASSERT_EQ(linhas.size(), 1u);
    EXPECT_EQ(std::get<std::string>(linhas[0][0]), "a primeira nota");
}

// Consequência da anterior: como nada foi registrado, a subida seguinte tenta de
// novo — e com a migração corrigida, aplica.
TEST_F(MigrationFailureTest, NextBootRetriesAndSucceeds)
{
    EXPECT_FALSE(schema->syncEntity<NotaComMigracaoQuebrada>());

    struct NotaCorrigida : NotaComMigracaoQuebrada {
        [[nodiscard]] std::vector<Migration> migrations() const override
        {
            return {{2, "autor", "ALTER TABLE notas ADD COLUMN autor TEXT", ""}};
        }
    };

    EXPECT_TRUE(schema->syncEntity<NotaCorrigida>());
    EXPECT_EQ(versaoRegistrada(), 2);
    EXPECT_TRUE(temColuna("autor"));
}

// Antes isto passava como sucesso e não fazia nada: o laço não encontrava
// migração no intervalo, o commit acontecia, e a tabela ficava no formato antigo
// com a versão registrada parada. Toda subida repetia o não-fazer-nada, calada.
TEST_F(MigrationFailureTest, DeclaredVersionWithoutMigrationIsAnError)
{
    EXPECT_FALSE(schema->syncEntity<NotaComVersaoSemMigracao>());

    EXPECT_EQ(versaoRegistrada(), 1);
    EXPECT_NE(schema->lastError().find("versão 3"), std::string::npos) << schema->lastError();
    EXPECT_NE(schema->lastError().find("migrations()"), std::string::npos) << schema->lastError();
}

// E a mensagem serve para alguém: ela diz qual migração falhou, não só que algo
// falhou. Diagnóstico de esquema costuma chegar por log de campo, sem depurador.
TEST_F(MigrationFailureTest, ErrorMessageNamesTheFailedMigration)
{
    EXPECT_FALSE(schema->syncEntity<NotaComMigracaoQuebrada>());

    EXPECT_NE(schema->lastError().find("migração 2"), std::string::npos) << schema->lastError();
    EXPECT_NE(schema->lastError().find("autor"), std::string::npos) << schema->lastError();
}
