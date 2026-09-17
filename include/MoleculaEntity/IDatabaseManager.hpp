#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <variant>
#include <memory>

namespace MoleculaEntity {

using DbValue = std::variant<std::nullptr_t, int64_t, double, std::string>;
using DbRow = std::vector<DbValue>;
using DbResult = std::vector<DbRow>;

class IDatabaseManager {
public:
    virtual ~IDatabaseManager() = default;

    virtual bool execute(const std::string& sql) = 0;
    virtual bool execute(const std::string& sql, const std::vector<DbValue>& params) = 0;
    virtual DbResult query(const std::string& sql) = 0;
    virtual DbResult query(const std::string& sql, const std::vector<DbValue>& params) = 0;
    virtual int64_t lastInsertRowId() = 0;
    virtual int affectedRows() = 0;
    virtual bool beginTransaction() = 0;
    virtual bool commit() = 0;
    virtual bool rollback() = 0;

    /// Falso depois de um comando que falhou, até o próximo comando.
    ///
    /// Existe porque `query()` devolve linhas: uma consulta que falha e uma
    /// que não encontrou nada são as duas um vetor vazio, e sem isto não há
    /// como distinguir "não tem" de "não deu".
    [[nodiscard]] virtual bool ok() const noexcept = 0;

    /// A mensagem do banco para a última falha, vazia quando não houve.
    [[nodiscard]] virtual const std::string& lastError() const noexcept = 0;
};

// NOTA: aqui existia um `escapeString`. Ele saiu, e a remoção é proposital:
// nada na biblioteca o chamava, e o que ele oferecia era o caminho para montar
// SQL concatenando texto — exatamente o que os `?` desta interface e o
// `quoteIdentifier` existem para tornar desnecessário. Interface de biblioteca
// pública não deve carregar o método que serve para usá-la errado.

using DatabaseManagerPtr = std::shared_ptr<IDatabaseManager>;

} // namespace MoleculaEntity
