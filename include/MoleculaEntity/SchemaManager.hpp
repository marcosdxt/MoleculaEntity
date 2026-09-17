#pragma once

#include "IDatabaseManager.hpp"
#include "BaseEntity.hpp"
#include "Identifier.hpp"
#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace MoleculaEntity {

class SchemaManager {
public:
    explicit SchemaManager(DatabaseManagerPtr db) : db_(std::move(db)) {}
    ~SchemaManager() = default;

    SchemaManager(const SchemaManager&) = delete;
    SchemaManager& operator=(const SchemaManager&) = delete;
    SchemaManager(SchemaManager&&) = default;
    SchemaManager& operator=(SchemaManager&&) = default;

    // Todo método que toca o banco devolve `bool`, e devolve [[nodiscard]].
    //
    // A assinatura é parte do conserto, não enfeite: enquanto `syncEntity` era
    // `void`, o chamador não TINHA como saber que a migração falhou — e o
    // próprio SchemaManager ignorava os retornos do driver. O resultado era o
    // pior dos mundos: a versão era registrada, a transação era confirmada, e o
    // banco passava a afirmar estar num esquema que não tinha. A subida seguinte
    // via a versão nova e nunca mais tentava; a coluna não chegava nunca, em
    // silêncio.
    //
    // Quando algo falha, `lastError()` diz o quê.
    [[nodiscard]] bool initialize() {
        return createMigrationsTable();
    }

    template<typename TEntity>
    [[nodiscard]] bool syncEntity() {
        TEntity entity;
        const std::string tableName = entity.tableName();
        const int targetVersion = entity.tableVersion();

        const auto existe = tableExists(tableName);
        if (!existe.has_value()) {
            return falhar("não consegui consultar o esquema: " + db_->lastError());
        }

        if (!*existe) {
            // Criar a tabela e registrar a versão são uma coisa só. Separados,
            // uma falha no registro deixaria a tabela existindo sem versão
            // nenhuma — e a próxima subida tentaria aplicar as migrações desde o
            // começo, sobre uma tabela que já está no formato final.
            if (!db_->beginTransaction()) {
                return falhar(db_->lastError());
            }
            if (!createTable(entity) || !recordMigration(tableName, targetVersion, "Initial table creation")) {
                const std::string motivo = db_->lastError();
                db_->rollback();
                return falhar(motivo);
            }
            if (!db_->commit()) {
                return falhar(db_->lastError());
            }
            erro_.clear();
            return true;
        }

        const auto atual = getTableVersion(tableName);
        if (!atual.has_value()) {
            return falhar("não consegui ler a versão da tabela: " + db_->lastError());
        }

        if (*atual >= targetVersion) {
            erro_.clear();
            return true;   // já está em dia
        }

        return runMigrations(entity, *atual, targetVersion);
    }

    template<typename TEntity>
    [[nodiscard]] bool dropTable() {
        TEntity entity;
        std::string sql = "DROP TABLE IF EXISTS " + quoteIdentifier(entity.tableName());
        if (!db_->execute(sql)) {
            return falhar(db_->lastError());
        }
        erro_.clear();
        return true;
    }

    /// O motivo da última falha, vazio quando não houve.
    [[nodiscard]] const std::string& lastError() const noexcept { return erro_; }

    /// Vazio quando a consulta ao esquema falhou — que é diferente de "a tabela
    /// não existe". Sem essa distinção, um banco inacessível pareceria um banco
    /// vazio, e o passo seguinte seria tentar criar tudo de novo.
    [[nodiscard]] std::optional<bool> tableExists(const std::string& tableName) const {
        std::string sql = "SELECT name FROM sqlite_master WHERE type='table' AND name=?";
        auto result = db_->query(sql, {tableName});
        if (!db_->ok()) {
            return std::nullopt;
        }
        return !result.empty();
    }

    /// Vazio quando a consulta falhou. Zero significa "tabela sem migração
    /// registrada" — e confundir os dois faria um banco inacessível parecer um
    /// banco novo.
    [[nodiscard]] std::optional<int> getTableVersion(const std::string& tableName) const {
        std::string sql = "SELECT MAX(version) FROM __schema_migrations WHERE table_name = ?";
        auto result = db_->query(sql, {tableName});

        if (!db_->ok()) {
            return std::nullopt;
        }

        if (result.empty() || result[0].empty()) {
            return 0;
        }

        const auto& val = result[0][0];
        if (std::holds_alternative<std::nullptr_t>(val)) {
            return 0;
        }

        return static_cast<int>(std::get<int64_t>(val));
    }

private:
    bool createMigrationsTable() {
        std::string sql = R"(
            CREATE TABLE IF NOT EXISTS __schema_migrations (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                table_name TEXT NOT NULL,
                version INTEGER NOT NULL,
                description TEXT,
                applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE(table_name, version)
            )
        )";
        if (!db_->execute(sql)) {
            return falhar(db_->lastError());
        }
        erro_.clear();
        return true;
    }

    template<typename TEntity>
    bool createTable(const TEntity& entity) {
        std::ostringstream sql;
        sql << "CREATE TABLE IF NOT EXISTS " << quoteIdentifier(entity.tableName()) << " (";

        auto baseColumns = BaseEntity::baseColumns();
        auto entityColumns = entity.columns();

        std::vector<ColumnDefinition> allColumns;
        allColumns.reserve(baseColumns.size() + entityColumns.size());
        allColumns.insert(allColumns.end(), baseColumns.begin(), baseColumns.end());
        allColumns.insert(allColumns.end(), entityColumns.begin(), entityColumns.end());

        std::vector<std::string> foreignKeys;

        for (size_t i = 0; i < allColumns.size(); ++i) {
            const auto& col = allColumns[i];

            if (i > 0) sql << ", ";

            sql << quoteIdentifier(col.name) << " " << columnTypeToSql(col.type);

            if (col.primaryKey) {
                sql << " PRIMARY KEY AUTOINCREMENT";
            }

            if (!col.nullable && !col.primaryKey) {
                sql << " NOT NULL";
            }

            if (col.unique && !col.primaryKey) {
                sql << " UNIQUE";
            }

            if (col.defaultValue.has_value()) {
                sql << " DEFAULT " << col.defaultValue.value();
            }

            if (col.foreignKeyTable.has_value() && col.foreignKeyColumn.has_value()) {
                std::ostringstream fk;
                fk << "FOREIGN KEY (" << quoteIdentifier(col.name) << ") REFERENCES "
                   << quoteIdentifier(col.foreignKeyTable.value())
                   << "(" << quoteIdentifier(col.foreignKeyColumn.value()) << ")";
                foreignKeys.push_back(fk.str());
            }
        }

        for (const auto& fk : foreignKeys) {
            sql << ", " << fk;
        }

        sql << ")";

        db_->execute(sql.str());
        createUpdatedAtTrigger(entity.tableName());
        return createIdIndex(entity.tableName());
    }

    bool createUpdatedAtTrigger(const std::string& tableName) {
        std::ostringstream sql;
        sql << "CREATE TRIGGER IF NOT EXISTS " << quoteIdentifier(tableName + "_updated_at_trigger") << " "
            << "AFTER UPDATE ON " << quoteIdentifier(tableName) << " "
            << "FOR EACH ROW BEGIN "
            << "UPDATE " << quoteIdentifier(tableName) << " SET updated_at = CURRENT_TIMESTAMP "
            << "WHERE idx = OLD.idx; "
            << "END";
        return db_->execute(sql.str());
    }

    bool createIdIndex(const std::string& tableName) {
        std::ostringstream sql;
        sql << "CREATE INDEX IF NOT EXISTS " << quoteIdentifier("idx_" + tableName + "_id")
            << " ON " << quoteIdentifier(tableName) << "(\"id\")";
        return db_->execute(sql.str());
    }

    template<typename TEntity>
    bool runMigrations(const TEntity& entity, int fromVersion, int toVersion) {
        auto migrations = entity.migrations();

        if (!db_->beginTransaction()) {
            return falhar(db_->lastError());
        }

        int alcancada = fromVersion;

        // O `try` continua aqui por causa de driver de terceiro: a interface
        // não proíbe lançar, e um que lance no meio precisa desfazer a
        // transação também. Mas ele NÃO é o mecanismo — o mecanismo é conferir
        // cada retorno, logo abaixo. Confiar só no catch era o defeito: com um
        // driver que reporta erro devolvendo `false`, o catch nunca disparava.
        try {
            for (const auto& migration : migrations) {
                if (migration.version <= fromVersion || migration.version > toVersion) {
                    continue;
                }

                if (!db_->execute(migration.upSql)) {
                    return desfazer("migração " + std::to_string(migration.version) + " (" +
                                    migration.description + ") falhou: " + db_->lastError());
                }

                if (!recordMigration(entity.tableName(), migration.version, migration.description)) {
                    return desfazer("não consegui registrar a migração " +
                                    std::to_string(migration.version) + ": " + db_->lastError());
                }

                alcancada = migration.version;
            }
        } catch (...) {
            db_->rollback();
            erro_ = "o driver lançou durante a migração";
            throw;
        }

        // Versão declarada sem migração que chegue nela. Sem esta verificação, o
        // caso passava como sucesso e não fazia nada: a tabela ficava no formato
        // antigo, a versão registrada não subia, e toda subida repetia o
        // não-fazer-nada — calada.
        if (alcancada < toVersion) {
            return desfazer("a entidade declara versão " + std::to_string(toVersion) +
                            ", mas não há migração que saia da " + std::to_string(alcancada) +
                            ". Falta declará-la em migrations().");
        }

        if (!db_->commit()) {
            return falhar(db_->lastError());
        }

        erro_.clear();
        return true;
    }

    bool desfazer(std::string motivo) {
        db_->rollback();
        erro_ = std::move(motivo);
        return false;
    }

    bool falhar(std::string motivo) {
        erro_ = std::move(motivo);
        return false;
    }

    bool recordMigration(const std::string& tableName, int version, const std::string& description) {
        std::string sql = "INSERT INTO __schema_migrations (table_name, version, description) VALUES (?, ?, ?)";
        return db_->execute(sql, {tableName, static_cast<int64_t>(version), description});
    }

    [[nodiscard]] static std::string columnTypeToSql(ColumnType type) {
        switch (type) {
            case ColumnType::Integer: return "INTEGER";
            case ColumnType::BigInt: return "INTEGER";
            case ColumnType::Real: return "REAL";
            case ColumnType::Text: return "TEXT";
            case ColumnType::Blob: return "BLOB";
            case ColumnType::Boolean: return "INTEGER";
            case ColumnType::Timestamp: return "TIMESTAMP";
            default: return "TEXT";
        }
    }

    DatabaseManagerPtr db_;
    std::string erro_;
};

} // namespace MoleculaEntity
