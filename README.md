# MoleculaEntity

Framework de Entity/Repository para C++17 com SQLite3, inspirado no TypeORM. Oferece uma camada de abstração que elimina a necessidade de escrever SQL nos consumidores.

## Arquitetura

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Aplicação                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                         │
│  │UserRepository│  │OrderRepository│ │ProductRepository│   ...              │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘                         │
│         │                │                │                                 │
│         └────────────────┼────────────────┘                                 │
│                          │                                                  │
│                          ▼                                                  │
│                 ┌─────────────────┐                                         │
│                 │  BaseRepository │  ← CRUD genérico                        │
│                 │  <TEntity>      │                                         │
│                 └────────┬────────┘                                         │
│                          │                                                  │
│         ┌────────────────┼────────────────┐                                 │
│         │                │                │                                 │
│         ▼                ▼                ▼                                 │
│  ┌────────────┐  ┌─────────────┐  ┌─────────────┐                          │
│  │QueryBuilder│  │SchemaManager│  │ BaseEntity  │                          │
│  └────────────┘  └─────────────┘  └─────────────┘                          │
│                          │                                                  │
│                          ▼                                                  │
│                ┌──────────────────┐                                         │
│                │IDatabaseManager  │  ← Interface (injetada)                 │
│                │    (interface)   │                                         │
│                └────────┬─────────┘                                         │
│                         │                                                   │
└─────────────────────────┼───────────────────────────────────────────────────┘
                          │
                          ▼
              ┌───────────────────────┐
              │  SQLite3 / PostgreSQL │   Implementação concreta
              │  (sua implementação)  │   fornecida pela aplicação
              └───────────────────────┘
```

## Fluxo de Geração de Código

```
  schema.toml                    molecula_gen.py                    Código C++
 ┌───────────┐                  ┌───────────────┐                 ┌────────────┐
 │           │                  │               │                 │            │
 │ [entities]│                  │   Parser      │                 │ Entity.hpp │
 │ User      │ ───────────────► │      +        │ ──────────────► │            │
 │ Order     │                  │   Generator   │                 │ Repo.hpp   │
 │ Product   │                  │               │                 │            │
 │           │                  │               │                 │Bootstrap.hpp
 └───────────┘                  └───────────────┘                 └────────────┘

     TOML                           Python                          Headers
   (declarativo)                  (processamento)                  (compilável)
```

## Instalação

### Dependências

- C++17 ou superior
- CMake 3.14+
- SQLite3
- Google Test (para testes)
- Python 3.7+ com `tomli` (para o gerador)

```bash
pip install tomli
```

### Compilação

```bash
mkdir build && cd build
cmake .. -DMOLECULA_ENTITY_BUILD_TESTS=ON
cmake --build .
```

### Executar Testes

```bash
./components/MoleculaEntity/MoleculaEntity_tests
```

## Uso Rápido

### 1. Definir Schema (TOML)

```toml
# schema.toml
[config]
namespace = "MyApp"
output_dir = "generated"

[entities.User]
table = "users"
version = 1

[entities.User.columns]
name = { type = "string", nullable = false }
email = { type = "string", nullable = false, unique = true }
```

### 2. Gerar Código

```bash
python3 generator/molecula_gen.py schema.toml -o ./generated
```

### 3. Usar no C++

```cpp
#include "generated/Entities.hpp"
#include "MinhaSQLiteImpl.hpp"

int main() {
    auto db = std::make_shared<MinhaSQLiteImpl>("app.db");
    MyApp::DatabaseBootstrap bootstrap(db);
    bootstrap.syncAll();  // Cria/atualiza tabelas

    auto& userRepo = bootstrap.userRepository();

    // Criar
    MyApp::UserEntity user;
    user.setName("João");
    user.setEmail("joao@email.com");
    auto saved = userRepo.save(user);

    // Buscar
    auto found = userRepo.findByEmail("joao@email.com");
    if (found) {
        std::cout << found->getName() << std::endl;
    }

    return 0;
}
```

---

## Sintaxe do Schema TOML

### Estrutura Geral

```
schema.toml
│
├── [config]                    # Configurações globais
│
└── [entities.NomeDaEntidade]   # Definição de cada entidade
    ├── table = "nome_tabela"
    ├── version = N
    ├── [entities.Nome.columns]
    ├── [entities.Nome.indexes]
    ├── [entities.Nome.repository]
    └── [entities.Nome.migrations]
```

### Seção `[config]`

```toml
[config]
namespace = "MyApp"              # Namespace C++ para o código gerado
output_dir = "generated"         # Diretório de saída
include_guard_prefix = "MYAPP"   # Prefixo para include guards (opcional)
```

| Campo | Tipo | Obrigatório | Descrição |
|-------|------|-------------|-----------|
| `namespace` | string | Não | Namespace C++ (default: "Generated") |
| `output_dir` | string | Não | Diretório de saída (default: "generated") |
| `include_guard_prefix` | string | Não | Prefixo para guards (default: "GENERATED") |

### Seção `[entities.Nome]`

```toml
[entities.User]
table = "users"    # Nome da tabela no banco
version = 1        # Versão do schema (para migrações)
```

| Campo | Tipo | Obrigatório | Descrição |
|-------|------|-------------|-----------|
| `table` | string | Sim | Nome da tabela no banco de dados |
| `version` | int | Sim | Versão atual do schema |

### Seção `[entities.Nome.columns]`

Define as colunas da entidade (além das colunas base automáticas).

```toml
[entities.User.columns]
name = { type = "string", nullable = false }
email = { type = "string", nullable = false, unique = true }
age = { type = "int", nullable = true }
salary = { type = "double", nullable = true }
active = { type = "bool", nullable = false, default = "true" }
created_by = { type = "int64", nullable = false, foreign_key = { table = "admins", column = "idx" } }
```

#### Tipos Suportados

| Tipo TOML | Tipo C++ | Tipo SQLite |
|-----------|----------|-------------|
| `string` | `std::string` | `TEXT` |
| `int` | `int` | `INTEGER` |
| `int32` | `int32_t` | `INTEGER` |
| `int64` | `int64_t` | `INTEGER` |
| `double` | `double` | `REAL` |
| `float` | `float` | `REAL` |
| `bool` | `bool` | `INTEGER` |
| `blob` | `std::vector<uint8_t>` | `BLOB` |
| `timestamp` | `BaseEntity::Timestamp` | `TIMESTAMP` |

#### Propriedades das Colunas

| Propriedade | Tipo | Default | Descrição |
|-------------|------|---------|-----------|
| `type` | string | - | Tipo da coluna (obrigatório) |
| `nullable` | bool | `true` | Permite NULL |
| `unique` | bool | `false` | Valor único |
| `default` | string | - | Valor default SQL |
| `foreign_key` | object | - | Chave estrangeira |

#### Foreign Key

```toml
user_id = {
    type = "int64",
    nullable = false,
    foreign_key = { table = "users", column = "idx" }
}
```

### Seção `[entities.Nome.indexes]`

```toml
[entities.User.indexes]
email_idx = { columns = ["email"], unique = true }
name_age_idx = { columns = ["name", "age"] }
```

| Propriedade | Tipo | Default | Descrição |
|-------------|------|---------|-----------|
| `columns` | array | - | Lista de colunas do índice |
| `unique` | bool | `false` | Índice único |

### Seção `[entities.Nome.repository]`

Define métodos customizados do repositório.

```toml
[entities.User.repository]
findByEmail = { where = [{ column = "email", op = "eq" }], returns = "optional" }
findActiveUsers = { where = [{ column = "active", op = "eq", value = "true" }], returns = "vector" }
findByAgeRange = { where = [{ column = "age", op = "between" }], returns = "vector" }
findByStatus = { where = [{ column = "status", op = "in" }], returns = "vector" }
```

#### Estrutura do Método

```toml
nomeDoMetodo = {
    where = [...],      # Condições WHERE
    returns = "..."     # Tipo de retorno
}
```

#### Condições WHERE

| Campo | Descrição |
|-------|-----------|
| `column` | Nome da coluna |
| `op` | Operador de comparação |
| `value` | Valor fixo (opcional - se omitido, vira parâmetro) |

#### Operadores Suportados

| Operador | SQL Gerado | Parâmetros |
|----------|------------|------------|
| `eq` | `= ?` | 1 |
| `neq` | `!= ?` | 1 |
| `gt` | `> ?` | 1 |
| `gte` | `>= ?` | 1 |
| `lt` | `< ?` | 1 |
| `lte` | `<= ?` | 1 |
| `like` | `LIKE ?` | 1 |
| `notlike` | `NOT LIKE ?` | 1 |
| `in` | `IN (?, ?, ...)` | N |
| `notin` | `NOT IN (?, ?, ...)` | N |
| `null` | `IS NULL` | 0 |
| `notnull` | `IS NOT NULL` | 0 |
| `between` | `BETWEEN ? AND ?` | 2 |

#### Tipos de Retorno

| Valor | Tipo C++ Gerado |
|-------|-----------------|
| `optional` | `std::optional<Entity>` |
| `vector` | `std::vector<Entity>` |

### Seção `[entities.Nome.migrations]`

Define migrações para evoluir o schema.

```toml
[entities.User.migrations]
2 = { description = "Add phone column", up = "ALTER TABLE users ADD COLUMN phone TEXT", down = "ALTER TABLE users DROP COLUMN phone" }
3 = { description = "Add address", up = "ALTER TABLE users ADD COLUMN address TEXT" }
```

| Campo | Tipo | Descrição |
|-------|------|-----------|
| `description` | string | Descrição da migração |
| `up` | string | SQL para aplicar a migração |
| `down` | string | SQL para reverter (opcional) |

---

## Gerador de Código

### Uso

```bash
python3 molecula_gen.py <schema_file> [--output-dir <dir>]
```

### Argumentos

| Argumento | Descrição |
|-----------|-----------|
| `schema_file` | Caminho para o arquivo `.toml` ou `.json` |
| `--output-dir`, `-o` | Diretório de saída (sobrescreve config) |

### Exemplos

```bash
# Usando configuração do TOML
python3 molecula_gen.py schema.toml

# Especificando diretório de saída
python3 molecula_gen.py schema.toml -o ./src/generated

# Usando JSON
python3 molecula_gen.py schema.json -o ./generated
```

### Arquivos Gerados

```
output_dir/
├── Entities.hpp           # Header principal (inclui todos)
├── DatabaseBootstrap.hpp  # Classe de bootstrap/sync
├── UserEntity.hpp         # Entidade User
├── UserRepository.hpp     # Repositório User
├── OrderEntity.hpp        # Entidade Order
├── OrderRepository.hpp    # Repositório Order
└── ...
```

### Diagrama de Arquivos Gerados

```
                         Entities.hpp
                              │
           ┌──────────────────┼──────────────────┐
           │                  │                  │
           ▼                  ▼                  ▼
    UserEntity.hpp     OrderEntity.hpp    ProductEntity.hpp
           │                  │                  │
           ▼                  ▼                  ▼
   UserRepository.hpp  OrderRepository.hpp ProductRepository.hpp
           │                  │                  │
           └──────────────────┼──────────────────┘
                              │
                              ▼
                    DatabaseBootstrap.hpp
                              │
                              ▼
                    ┌─────────────────┐
                    │   syncAll()     │ ─── Cria/atualiza tabelas
                    │   userRepo()    │ ─── Acesso aos repositórios
                    │   orderRepo()   │
                    │   productRepo() │
                    └─────────────────┘
```

---

## Colunas Base Automáticas

Toda entidade herda automaticamente estas colunas:

```
┌─────────────────────────────────────────────────────────────┐
│                      BaseEntity                             │
├─────────────┬──────────────┬────────────────────────────────┤
│ Campo       │ Tipo         │ Descrição                      │
├─────────────┼──────────────┼────────────────────────────────┤
│ idx         │ INTEGER PK   │ Chave primária autoincrement   │
│ id          │ TEXT UNIQUE  │ UUID v4 gerado automaticamente │
│ created_at  │ TIMESTAMP    │ Data de criação (automático)   │
│ updated_at  │ TIMESTAMP    │ Última atualização (trigger)   │
└─────────────┴──────────────┴────────────────────────────────┘
```

---

## QueryBuilder

O `QueryBuilder` permite construir queries sem SQL:

```cpp
QueryBuilder qb;

// WHERE simples
qb.where("name", CompareOp::Equals, std::string("João"));

// Múltiplas condições
qb.where("active", CompareOp::Equals, 1)
  .andWhere("age", CompareOp::GreaterThan, 18)
  .orWhere("role", CompareOp::Equals, std::string("admin"));

// BETWEEN
qb.whereBetween("age", 18, 65);

// IN
qb.whereIn("status", {std::string("active"), std::string("pending")});

// NULL checks
qb.whereNull("deleted_at");
qb.whereNotNull("verified_at");

// ORDER BY
qb.orderBy("name", OrderDirection::Asc)
  .orderBy("created_at", OrderDirection::Desc);

// LIMIT e OFFSET
qb.limit(10).offset(20);

// Usar com repositório
auto results = userRepo.find(qb);
```

### Diagrama de Operadores

```
┌────────────────────────────────────────────────────────────────┐
│                        QueryBuilder                            │
├────────────────────────────────────────────────────────────────┤
│                                                                │
│  Comparação:          Lógicos:           Ordenação:            │
│  ┌──────────────┐     ┌──────────┐       ┌─────────────┐       │
│  │ Equals       │     │ AND      │       │ ASC         │       │
│  │ NotEquals    │     │ OR       │       │ DESC        │       │
│  │ GreaterThan  │     └──────────┘       └─────────────┘       │
│  │ GreaterThanEq│                                              │
│  │ LessThan     │     Nulidade:          Paginação:            │
│  │ LessThanEq   │     ┌──────────┐       ┌─────────────┐       │
│  │ Like         │     │ IsNull   │       │ LIMIT       │       │
│  │ NotLike      │     │ IsNotNull│       │ OFFSET      │       │
│  │ In           │     └──────────┘       └─────────────┘       │
│  │ NotIn        │                                              │
│  │ Between      │                                              │
│  └──────────────┘                                              │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

---

## Implementando IDatabaseManager

Você precisa fornecer uma implementação da interface `IDatabaseManager`:

```cpp
#include <MoleculaEntity/IDatabaseManager.hpp>
#include <sqlite3.h>

class SQLite3Manager : public MoleculaEntity::IDatabaseManager {
public:
    explicit SQLite3Manager(const std::string& path) {
        sqlite3_open(path.c_str(), &db_);
    }

    ~SQLite3Manager() override {
        sqlite3_close(db_);
    }

    bool execute(const std::string& sql) override {
        return sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    bool execute(const std::string& sql, const std::vector<DbValue>& params) override {
        // Implementar com prepared statements
    }

    DbResult query(const std::string& sql) override {
        // Implementar
    }

    DbResult query(const std::string& sql, const std::vector<DbValue>& params) override {
        // Implementar
    }

    int64_t lastInsertRowId() override {
        return sqlite3_last_insert_rowid(db_);
    }

    int affectedRows() override {
        return sqlite3_changes(db_);
    }

    bool beginTransaction() override {
        return execute("BEGIN TRANSACTION");
    }

    bool commit() override {
        return execute("COMMIT");
    }

    bool rollback() override {
        return execute("ROLLBACK");
    }

    std::string escapeString(const std::string& str) override {
        // Implementar escape de aspas
    }

private:
    sqlite3* db_ = nullptr;
};
```

---

## Exemplo Completo

### Schema (`schema.toml`)

```toml
[config]
namespace = "ECommerce"
output_dir = "src/generated"

[entities.Customer]
table = "customers"
version = 1

[entities.Customer.columns]
name = { type = "string", nullable = false }
email = { type = "string", nullable = false, unique = true }
phone = { type = "string", nullable = true }
active = { type = "bool", nullable = false, default = "true" }

[entities.Customer.repository]
findByEmail = { where = [{ column = "email", op = "eq" }], returns = "optional" }
findActive = { where = [{ column = "active", op = "eq", value = "true" }], returns = "vector" }

[entities.Order]
table = "orders"
version = 1

[entities.Order.columns]
customer_id = { type = "int64", nullable = false, foreign_key = { table = "customers", column = "idx" } }
total = { type = "double", nullable = false }
status = { type = "string", nullable = false, default = "'pending'" }

[entities.Order.repository]
findByCustomerId = { where = [{ column = "customer_id", op = "eq" }], returns = "vector" }
findByStatus = { where = [{ column = "status", op = "eq" }], returns = "vector" }
findPending = { where = [{ column = "status", op = "eq", value = "'pending'" }], returns = "vector" }
```

### Uso (`main.cpp`)

```cpp
#include "src/generated/Entities.hpp"
#include "SQLite3Manager.hpp"

int main() {
    // Setup
    auto db = std::make_shared<SQLite3Manager>("ecommerce.db");
    ECommerce::DatabaseBootstrap app(db);
    app.syncAll();

    // Repositórios
    auto& customers = app.customerRepository();
    auto& orders = app.orderRepository();

    // Criar cliente
    ECommerce::CustomerEntity customer;
    customer.setName("Maria Silva");
    customer.setEmail("maria@email.com");
    customer.setPhone("+55 11 99999-0000");
    auto savedCustomer = customers.save(customer);

    std::cout << "Cliente criado com ID: " << savedCustomer.getId() << std::endl;

    // Criar pedido
    ECommerce::OrderEntity order;
    order.setCustomerId(savedCustomer.getIdx().value());
    order.setTotal(299.90);
    order.setStatus("confirmed");
    orders.save(order);

    // Buscar pedidos pendentes
    auto pending = orders.findPending();
    std::cout << "Pedidos pendentes: " << pending.size() << std::endl;

    // Query customizada
    MoleculaEntity::QueryBuilder qb;
    qb.where("total", MoleculaEntity::CompareOp::GreaterThan, 100.0)
      .orderBy("total", MoleculaEntity::OrderDirection::Desc)
      .limit(10);

    auto bigOrders = orders.find(qb);

    return 0;
}
```

---

## Licença

MIT License
