#pragma once

/// O driver de SQLite3 da biblioteca.
///
/// Este é o único cabeçalho que inclui <sqlite3.h>, e por isso é o único que
/// obriga a linkar SQLite. Quem usa outro banco implementa `IDatabaseManager`
/// e nunca inclui este arquivo; no CMake, o alvo é `MoleculaEntity::SQLite3`,
/// separado de `MoleculaEntity` justamente para que a escolha seja explícita.
///
/// **Nada aqui lança.** Erro vira `false` (ou resultado vazio) mais `ok()` e
/// `lastError()`. O motivo não é gosto: esta biblioteca é usada dentro de
/// serviços que não podem desenrolar a pilha no caminho de I/O — e uma
/// biblioteca que lança onde o chamador não pode tratar obriga o chamador a
/// embrulhar cada chamada num try/catch, o que ninguém faz até o dia do
/// primeiro `terminate`.
///
/// Para quem prefere exceção, `open()` devolvendo nulo é fácil de transformar
/// numa; o contrário não é.

#include "IDatabaseManager.hpp"

#include <sqlite3.h>

#include <chrono>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace MoleculaEntity {

class SQLite3DatabaseManager final : public IDatabaseManager {
public:
    /// O que o SQLite decide na abertura e que muda o comportamento do banco
    /// inteiro. Os padrões são os de um serviço que grava em disco de verdade,
    /// não os de um teste.
    struct Options {
        /// WAL: leitores não bloqueiam o escritor nem o contrário. Num
        /// processo com uma thread gravando e outra lendo — o caso normal de um
        /// serviço — sem isto as leituras batem em SQLITE_BUSY.
        /// Bancos `:memory:` ignoram (WAL exige arquivo).
        bool walJournal = true;

        /// Quanto esperar por um lock antes de desistir. O padrão do SQLite é
        /// ZERO: qualquer concorrência devolve SQLITE_BUSY na hora, e o
        /// sintoma é um erro intermitente que não reproduz na bancada.
        std::chrono::milliseconds busyTimeout{5000};

        /// `Full` sobrevive a queda de energia sem corromper; `Normal` com WAL
        /// sobrevive a queda de PROCESSO, e pode perder a última transação numa
        /// queda de energia. Em equipamento que desliga sem avisar, escolha
        /// `Full` — o custo é uma sincronização a mais por commit.
        enum class Synchronous { Off, Normal, Full };
        Synchronous synchronous = Synchronous::Full;

        /// Chave estrangeira só é verificada se for ligada por conexão. O
        /// padrão do SQLite é desligado, por compatibilidade histórica.
        bool foreignKeys = true;

        /// Recusa string entre aspas duplas onde o SQL pede identificador.
        ///
        /// O SQLite, por compatibilidade antiga com o MySQL, aceita `"texto"`
        /// como literal quando não existe coluna com esse nome. O efeito é
        /// perverso para quem cita identificadores: um nome de coluna errado
        /// deixa de ser erro e vira uma comparação contra a string com o nome
        /// da coluna — a consulta roda, não devolve nada, e ninguém fica
        /// sabendo. Desligado, o banco responde "no such column", que é a
        /// verdade.
        ///
        /// Exige SQLite ≥ 3.29; em versões anteriores a opção é ignorada.
        bool strictIdentifiers = true;
    };

    /// Abre o banco. Devolve nulo — sem lançar — quando não dá, e escreve o
    /// motivo em `error`, se for passado.
    ///
    ///     std::string erro;
    ///     auto db = SQLite3DatabaseManager::open("/var/lib/app/dados.db", {}, &erro);
    ///     if (!db) { /* `erro` diz o quê */ }
    /// Com as opções padrão. Sobrecarga em vez de argumento com valor padrão
    /// porque `Options` é uma classe aninhada: dentro do corpo da classe que a
    /// contém ela ainda não está completa, e `= {}` num parâmetro não compila.
    [[nodiscard]] static std::shared_ptr<SQLite3DatabaseManager>
    open(const std::string& path, std::string* error = nullptr)
    {
        return open(path, Options{}, error);
    }

    [[nodiscard]] static std::shared_ptr<SQLite3DatabaseManager>
    open(const std::string& path, const Options& options, std::string* error = nullptr)
    {
        sqlite3* handle = nullptr;
        const int rc = sqlite3_open(path.c_str(), &handle);
        if (rc != SQLITE_OK) {
            if (error != nullptr) {
                // sqlite3_open devolve um handle mesmo falhando, para que a
                // mensagem possa ser lida; fechá-lo é obrigação nossa.
                *error = handle != nullptr ? sqlite3_errmsg(handle) : "sqlite3_open falhou";
            }
            sqlite3_close(handle);
            return nullptr;
        }

        auto db = std::shared_ptr<SQLite3DatabaseManager>(new SQLite3DatabaseManager(handle));
        if (!db->applyOptions(options)) {
            if (error != nullptr) {
                *error = db->lastError();
            }
            return nullptr;
        }
        return db;
    }

    ~SQLite3DatabaseManager() override
    {
        if (db_ != nullptr) {
            sqlite3_close(db_);
        }
    }

    SQLite3DatabaseManager(const SQLite3DatabaseManager&) = delete;
    SQLite3DatabaseManager& operator=(const SQLite3DatabaseManager&) = delete;

    SQLite3DatabaseManager(SQLite3DatabaseManager&& other) noexcept
        : db_(std::exchange(other.db_, nullptr))
        , lastError_(std::move(other.lastError_))
        , ok_(other.ok_) {}

    SQLite3DatabaseManager& operator=(SQLite3DatabaseManager&& other) noexcept
    {
        if (this != &other) {
            if (db_ != nullptr) {
                sqlite3_close(db_);
            }
            db_ = std::exchange(other.db_, nullptr);
            lastError_ = std::move(other.lastError_);
            ok_ = other.ok_;
        }
        return *this;
    }

    bool execute(const std::string& sql) override { return execute(sql, {}); }

    bool execute(const std::string& sql, const std::vector<DbValue>& params) override
    {
        Statement stmt(*this, sql, params);
        if (!stmt) {
            return false;
        }

        const int rc = sqlite3_step(stmt.get());
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            return fail(sql);
        }

        return succeed();
    }

    DbResult query(const std::string& sql) override { return query(sql, {}); }

    DbResult query(const std::string& sql, const std::vector<DbValue>& params) override
    {
        DbResult result;

        Statement stmt(*this, sql, params);
        if (!stmt) {
            return result;
        }

        const int columns = sqlite3_column_count(stmt.get());

        int rc = SQLITE_OK;
        while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
            DbRow row;
            row.reserve(static_cast<std::size_t>(columns));

            for (int i = 0; i < columns; ++i) {
                row.push_back(readColumn(stmt.get(), i));
            }

            result.push_back(std::move(row));
        }

        if (rc != SQLITE_DONE) {
            fail(sql);
            return DbResult{};   // vazio E ok()==false: ver a nota de `ok()`
        }

        succeed();
        return result;
    }

    int64_t lastInsertRowId() override { return sqlite3_last_insert_rowid(db_); }

    int affectedRows() override { return sqlite3_changes(db_); }

    bool beginTransaction() override { return execute("BEGIN TRANSACTION"); }
    bool commit() override { return execute("COMMIT"); }
    bool rollback() override { return execute("ROLLBACK"); }

    [[nodiscard]] bool ok() const noexcept override { return ok_; }
    [[nodiscard]] const std::string& lastError() const noexcept override { return lastError_; }

    /// O handle cru, para o que a interface não cobre (backup online, funções
    /// definidas pelo usuário, `sqlite3_wal_checkpoint`). Continua sendo nosso:
    /// não feche.
    [[nodiscard]] sqlite3* handle() const noexcept { return db_; }

private:
    explicit SQLite3DatabaseManager(sqlite3* handle) noexcept : db_(handle) {}

    /// RAII em cima do statement: sem isto, cada caminho de erro precisa
    /// lembrar do `sqlite3_finalize`, e um `return` esquecido vaza o statement
    /// e segura o lock do banco até o processo morrer.
    class Statement {
    public:
        Statement(SQLite3DatabaseManager& owner, const std::string& sql,
                  const std::vector<DbValue>& params)
        {
            if (sqlite3_prepare_v2(owner.db_, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
                owner.fail(sql);
                return;
            }
            bind(params);
        }

        ~Statement()
        {
            if (stmt_ != nullptr) {
                sqlite3_finalize(stmt_);
            }
        }

        Statement(const Statement&) = delete;
        Statement& operator=(const Statement&) = delete;

        explicit operator bool() const noexcept { return stmt_ != nullptr; }
        [[nodiscard]] sqlite3_stmt* get() const noexcept { return stmt_; }

    private:
        void bind(const std::vector<DbValue>& params)
        {
            for (std::size_t i = 0; i < params.size(); ++i) {
                const auto index = static_cast<int>(i + 1);

                std::visit([this, index](auto&& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::nullptr_t>) {
                        sqlite3_bind_null(stmt_, index);
                    } else if constexpr (std::is_same_v<T, int64_t>) {
                        sqlite3_bind_int64(stmt_, index, value);
                    } else if constexpr (std::is_same_v<T, double>) {
                        sqlite3_bind_double(stmt_, index, value);
                    } else {
                        // SQLITE_TRANSIENT: o SQLite copia. Sem isso ele guarda
                        // o ponteiro, e o texto pode ser um temporário que
                        // morre antes do step.
                        sqlite3_bind_text(stmt_, index, value.c_str(),
                                          static_cast<int>(value.size()), SQLITE_TRANSIENT);
                    }
                }, params[i]);
            }
        }

        sqlite3_stmt* stmt_ = nullptr;
    };

    [[nodiscard]] static DbValue readColumn(sqlite3_stmt* stmt, int index)
    {
        switch (sqlite3_column_type(stmt, index)) {
            case SQLITE_INTEGER:
                return static_cast<int64_t>(sqlite3_column_int64(stmt, index));
            case SQLITE_FLOAT:
                return sqlite3_column_double(stmt, index);
            case SQLITE_TEXT: {
                const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, index));
                const int size = sqlite3_column_bytes(stmt, index);
                return text != nullptr ? std::string(text, static_cast<std::size_t>(size))
                                       : std::string{};
            }
            case SQLITE_BLOB: {
                // `DbValue` ainda não tem variante binária; o BLOB vem como
                // string de bytes, que preserva o conteúdo (inclusive `\0`,
                // porque o tamanho vem à parte) mas não distingue de TEXT na
                // volta. Coluna BLOB continua utilizável; tipagem fiel é
                // trabalho para quando `DbValue` ganhar a variante.
                const auto* bytes = static_cast<const char*>(sqlite3_column_blob(stmt, index));
                const int size = sqlite3_column_bytes(stmt, index);
                return bytes != nullptr ? std::string(bytes, static_cast<std::size_t>(size))
                                        : std::string{};
            }
            case SQLITE_NULL:
            default:
                return nullptr;
        }
    }

    bool applyOptions(const Options& options)
    {
        if (options.strictIdentifiers) {
#ifdef SQLITE_DBCONFIG_DQS_DML
            sqlite3_db_config(db_, SQLITE_DBCONFIG_DQS_DML, 0, nullptr);
#endif
#ifdef SQLITE_DBCONFIG_DQS_DDL
            sqlite3_db_config(db_, SQLITE_DBCONFIG_DQS_DDL, 0, nullptr);
#endif
        }

        if (options.foreignKeys && !execute("PRAGMA foreign_keys = ON")) {
            return false;
        }

        if (options.busyTimeout.count() > 0) {
            sqlite3_busy_timeout(db_, static_cast<int>(options.busyTimeout.count()));
        }

        // WAL não existe para banco em memória, e pedi-lo ali devolve o modo
        // que ficou valendo em vez de erro — então não tratamos como falha.
        if (options.walJournal) {
            execute("PRAGMA journal_mode = WAL");
        }

        switch (options.synchronous) {
            case Options::Synchronous::Off:    return execute("PRAGMA synchronous = OFF");
            case Options::Synchronous::Normal: return execute("PRAGMA synchronous = NORMAL");
            case Options::Synchronous::Full:   break;
        }
        return execute("PRAGMA synchronous = FULL");
    }

    bool fail(const std::string& sql)
    {
        lastError_ = std::string(sqlite3_errmsg(db_)) + " — SQL: " + sql;
        ok_ = false;
        return false;
    }

    bool succeed()
    {
        lastError_.clear();
        ok_ = true;
        return true;
    }

    sqlite3* db_ = nullptr;
    std::string lastError_;
    bool ok_ = true;
};

}  // namespace MoleculaEntity
