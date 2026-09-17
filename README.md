# MoleculaEntity

Entity/Repository em **C++17 para SQLite**, com gerador de código a partir de um
schema declarativo. Você descreve as tabelas num TOML; ele gera as entidades, os
repositórios e as migrações — e o seu código para de escrever SQL.

[![CI](https://github.com/marcosdxt/MoleculaEntity/actions/workflows/ci.yml/badge.svg)](https://github.com/marcosdxt/MoleculaEntity/actions/workflows/ci.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

```cpp
#include <MoleculaEntity/SQLite3DatabaseManager.hpp>
#include "generated/Entities.hpp"

auto db = MoleculaEntity::SQLite3DatabaseManager::open("app.db");   // não lança
if (!db) { /* trate */ }

MyApp::DatabaseBootstrap(db).syncAll();      // cria tabelas e aplica migrações

MyApp::UserRepository users(db);

MyApp::UserEntity user;
user.setName("Ada");
user.setEmail("ada@exemplo.com");
user = users.save(user);                     // INSERT, uuid e carimbos

if (auto achada = users.findByEmail("ada@exemplo.com")) {
    std::printf("%s, criada em %s\n",
                achada->getName().c_str(),
                MoleculaEntity::time_utils::formatUtc(*achada->getCreatedAt()).c_str());
}
```

## O que é, e o que não é

**É** uma camada fina sobre SQL, de cabeçalho só, para quem tem um SQLite embutido
na aplicação e não quer espalhar `sqlite3_prepare_v2` pelo código. Entidades com
identidade dupla (`idx` inteiro e `id` uuid), CRUD genérico, um construtor de
consultas tipado, migrações versionadas por tabela, e um gerador que transforma
schema em código.

**Não é** um ORM com mapeamento de relacionamentos, *lazy loading*, cache de
identidade ou dialeto múltiplo. Não há `JOIN` no construtor de consultas — quando
precisar de um, escreva o SQL e use `IDatabaseManager::query`, que continua ali
para isso.

Dependências: nenhuma para a biblioteca; SQLite3 para o driver que vem junto;
Python 3.11+ (ou 3.7+ com `tomli`) para o gerador, e **só na hora de gerar** — o
código gerado não depende de Python.

## Começando

### Como dependência (recomendado)

```cmake
include(FetchContent)
FetchContent_Declare(MoleculaEntity
    GIT_REPOSITORY https://github.com/marcosdxt/MoleculaEntity.git
    GIT_TAG v0.1.0)
FetchContent_MakeAvailable(MoleculaEntity)

target_link_libraries(sua_app PRIVATE MoleculaEntity::SQLite3)
```

Dois alvos, e a escolha é explícita:

| Alvo | O que traz |
|---|---|
| `MoleculaEntity::MoleculaEntity` | só os cabeçalhos, **sem dependência nenhuma** — para quem implementa `IDatabaseManager` sobre outro banco |
| `MoleculaEntity::SQLite3` | o de cima mais o driver de SQLite que vem na caixa (linka `SQLite::SQLite3`) |

### Instalado no sistema

```sh
cmake -S . -B build -DMOLECULA_BUILD_TESTS=OFF
cmake --build build --target install
```

```cmake
find_package(MoleculaEntity REQUIRED)
target_link_libraries(sua_app PRIVATE MoleculaEntity::SQLite3)
```

### Construindo e testando o próprio projeto

```sh
git clone https://github.com/marcosdxt/MoleculaEntity.git
cd MoleculaEntity
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Não precisa instalar GoogleTest nem rodar o gerador à mão: o CMake usa o
GoogleTest do sistema quando existe e o baixa quando não, e chama o gerador
durante o build.

## Os três passos

### 1. Descreva o schema

```toml
# schema.toml
[config]
namespace = "MyApp"
output_dir = "generated"

[entities.User]
table = "users"
version = 1

[entities.User.columns]
name  = { type = "string", nullable = false }
email = { type = "string", nullable = false, unique = true }
age   = { type = "int",    nullable = true }

[entities.User.repository]
findByEmail = { where = [{ column = "email", op = "eq" }], returns = "optional" }
```

### 2. Gere

```sh
python3 generator/molecula_gen.py schema.toml -o generated
```

Ou, melhor, no seu CMake — assim o código gerado nunca fica velho:

```cmake
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/Entities.hpp
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/generator/molecula_gen.py
            ${CMAKE_SOURCE_DIR}/schema.toml -o ${CMAKE_CURRENT_BINARY_DIR}/generated
    DEPENDS ${CMAKE_SOURCE_DIR}/schema.toml
    VERBATIM)
```

**Código gerado não se versiona.** Ele é derivado do schema, como um `.o` é
derivado do `.cpp` — versionar os dois é garantir que um dia eles discordem.

### 3. Use

O exemplo do topo desta página é o passo 3 inteiro.

## Os contratos

O que a biblioteca promete, e que vale conhecer antes de confiar nela.

### Erro não vira exceção

O driver de SQLite **não lança**. Erro vira `false` (ou resultado vazio), e o
motivo fica em `lastError()`:

```cpp
if (!db->execute("INSERT INTO ...", params)) {
    log(db->lastError());
}

const auto linhas = db->query("SELECT ...");
if (!db->ok()) { /* falhou */ }
else if (linhas.empty()) { /* rodou, não achou nada */ }
```

O `ok()` existe porque consulta que falha e consulta sem resultado são as duas um
vetor vazio. Sem ele, não há como distinguir "não tem" de "não deu".

`BaseRepository::save()` e `update()` **lançam** `std::runtime_error` quando a
escrita falha — é a exceção à regra, e está marcada aqui porque quem roda dentro
de um serviço que não pode desenrolar a pilha precisa saber.

### Tempo é UTC

`created_at` e `updated_at` são preenchidos pelo `CURRENT_TIMESTAMP` do SQLite,
que grava em **UTC**, e são lidos de volta como UTC. Nenhuma conversão consulta o
fuso da máquina. Para exibir, converta você — e para comparar ou gravar à mão:

```cpp
MoleculaEntity::time_utils::formatUtc(tp);   // "2026-09-16 23:14:11"
MoleculaEntity::time_utils::parseUtc(texto); // std::optional<time_point>
```

A suíte roda em `America/Sao_Paulo` e `Asia/Tokyo` na CI, além de UTC. Não é
zelo: a versão anterior lia o carimbo com `std::mktime`, deslocava tudo pelo fuso
local, e passava verde em toda CI do mundo — porque CI roda em UTC.

### Identificadores são citados

Valores vão sempre por `?`. Nomes de tabela e coluna não podem ir por `?` —
nenhum banco aceita —, então vão **entre aspas**, escapadas:

```cpp
qb.where("order", CompareOp::Equals, v);   //  WHERE "order" = ?
```

Duas consequências: coluna com nome de palavra reservada (`order`, `group`)
funciona, e nome de coluna vindo de fora não escapa para o SQL. O driver ainda
desliga o `DQS` do SQLite (`SQLITE_DBCONFIG_DQS_DML`), senão um identificador
inexistente viraria uma *string* em silêncio em vez de erro.

### Thread-safety

| | |
|---|---|
| `UuidGenerator::generate()` | **seguro** entre threads (motor por thread) |
| `QueryBuilder`, entidades | seguros enquanto cada thread tem a sua instância |
| `SQLite3DatabaseManager` | **uma conexão por thread.** Compartilhar exige serializar por fora |
| `BaseRepository` | segue a conexão que recebeu |

### O que o driver liga por padrão

```cpp
SQLite3DatabaseManager::Options opcoes;
opcoes.walJournal = true;                        // leitor não bloqueia escritor
opcoes.busyTimeout = std::chrono::seconds(5);    // o padrão do SQLite é ZERO
opcoes.synchronous = Options::Synchronous::Full; // sobrevive a queda de energia
opcoes.foreignKeys = true;                       // o padrão do SQLite é desligado
opcoes.strictIdentifiers = true;                 // "texto" não vira string

auto db = SQLite3DatabaseManager::open("app.db", opcoes, &erro);
```

Os padrões são os de um serviço que grava em disco de verdade. Em teste, `:memory:`
ignora WAL, e `synchronous` pode ir para `Off` se a suíte for grande.

## Referência

### Colunas base

Toda entidade nasce com quatro colunas, e elas não se declaram no schema:

| Coluna | Tipo | Para quê |
|---|---|---|
| `idx` | `INTEGER PRIMARY KEY AUTOINCREMENT` | a chave do banco, barata para índice e chave estrangeira |
| `id` | `TEXT UNIQUE` | uuid v4, gerado no `save()` — a identidade que atravessa processos e máquinas |
| `created_at` | `TIMESTAMP` | `CURRENT_TIMESTAMP` (UTC) |
| `updated_at` | `TIMESTAMP` | mantido por gatilho a cada UPDATE (UTC) |

São duas identidades de propósito: `idx` é rápido e local ao arquivo; `id` é
estável e pode ser combinado entre sistemas. Quem sincroniza com outro serviço usa
o `id`; quem liga tabelas usa o `idx`.

### QueryBuilder

```cpp
QueryBuilder qb;
qb.where("active", CompareOp::Equals, int64_t{1})
  .andWhere("age", CompareOp::GreaterThan, int64_t{18})
  .orWhere("role", CompareOp::Equals, std::string{"admin"})
  .whereIn("status", {std::string{"novo"}, std::string{"pago"}})
  .whereBetween("criado_em", a, b)
  .whereNull("apagado_em")
  .orderBy("nome", OrderDirection::Asc)
  .limit(20)
  .offset(40);
```

| Método | SQL |
|---|---|
| `where` / `andWhere` / `orWhere` | `= ?`, `!= ?`, `> ?`, `>= ?`, `< ?`, `<= ?`, `LIKE ?`, `NOT LIKE ?` |
| `whereIn` / `whereNotIn` | `IN (?, ?, …)` |
| `whereBetween` | `BETWEEN ? AND ?` |
| `whereNull` / `whereNotNull` | `IS NULL` / `IS NOT NULL` |
| `orderBy`, `limit`, `offset` | `ORDER BY`, `LIMIT`, `OFFSET` |

> **`AND` e `OR` não têm parênteses.** As condições são encadeadas na ordem em que
> foram adicionadas, e a precedência é a do SQL — `A AND B OR C` é `(A AND B) OR C`.
> Para agrupar de outro jeito, escreva o SQL.

### BaseRepository

| | |
|---|---|
| `save(e)` | INSERT se novo, UPDATE se já persistido. Gera o uuid quando falta |
| `update(e)` | UPDATE; lança se a entidade não foi persistida |
| `remove(e)` / `removeById(idx)` / `removeByUuid(id)` | DELETE |
| `findOne(idx)` / `findByUuid(id)` | `std::optional<Entity>` |
| `find(qb)` / `findAll()` | `std::vector<Entity>` |
| `count(qb)` / `exists(qb)` / `existsById` / `existsByUuid` | contagem e existência |

Os métodos que o gerador cria a partir de `[entities.Nome.repository]` entram ao
lado desses, com nome e assinatura que você escolheu.

### SchemaManager e migrações

```cpp
MoleculaEntity::SchemaManager schema(db);
schema.initialize();                 // cria __schema_migrations
schema.syncEntity<UserEntity>();     // CREATE TABLE IF NOT EXISTS + gatilho + índice
```

A versão aplicada de cada tabela fica em `__schema_migrations`. Ao subir a
`version` da entidade e declarar a migração, o `syncEntity` aplica o que falta —
**numa transação**, com rollback se algo falhar no meio.

```toml
[entities.User]
version = 2

[entities.User.migrations]
2 = { description = "telefone", up = "ALTER TABLE users ADD COLUMN phone TEXT" }
```

### Schema TOML, campo a campo

#### Estrutura Geral

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

#### Seção `[config]`

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

#### Seção `[entities.Nome]`

```toml
[entities.User]
table = "users"    # Nome da tabela no banco
version = 1        # Versão do schema (para migrações)
```

| Campo | Tipo | Obrigatório | Descrição |
|-------|------|-------------|-----------|
| `table` | string | Sim | Nome da tabela no banco de dados |
| `version` | int | Sim | Versão atual do schema |

#### Seção `[entities.Nome.columns]`

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

##### Tipos Suportados

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

##### Propriedades das Colunas

| Propriedade | Tipo | Default | Descrição |
|-------------|------|---------|-----------|
| `type` | string | - | Tipo da coluna (obrigatório) |
| `nullable` | bool | `true` | Permite NULL |
| `unique` | bool | `false` | Valor único |
| `default` | string | - | Valor default SQL |
| `foreign_key` | object | - | Chave estrangeira |

##### Foreign Key

```toml
user_id = {
    type = "int64",
    nullable = false,
    foreign_key = { table = "users", column = "idx" }
}
```

#### Seção `[entities.Nome.indexes]`

```toml
[entities.User.indexes]
email_idx = { columns = ["email"], unique = true }
name_age_idx = { columns = ["name", "age"] }
```

| Propriedade | Tipo | Default | Descrição |
|-------------|------|---------|-----------|
| `columns` | array | - | Lista de colunas do índice |
| `unique` | bool | `false` | Índice único |

#### Seção `[entities.Nome.repository]`

Define métodos customizados do repositório.

```toml
[entities.User.repository]
findByEmail = { where = [{ column = "email", op = "eq" }], returns = "optional" }
findActiveUsers = { where = [{ column = "active", op = "eq", value = "true" }], returns = "vector" }
findByAgeRange = { where = [{ column = "age", op = "between" }], returns = "vector" }
findByStatus = { where = [{ column = "status", op = "in" }], returns = "vector" }
```

##### Estrutura do Método

```toml
nomeDoMetodo = {
    where = [...],      # Condições WHERE
    returns = "..."     # Tipo de retorno
}
```

##### Condições WHERE

| Campo | Descrição |
|-------|-----------|
| `column` | Nome da coluna |
| `op` | Operador de comparação |
| `value` | Valor fixo (opcional - se omitido, vira parâmetro) |

##### Operadores Suportados

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

##### Tipos de Retorno

| Valor | Tipo C++ Gerado |
|-------|-----------------|
| `optional` | `std::optional<Entity>` |
| `vector` | `std::vector<Entity>` |

#### Seção `[entities.Nome.migrations]`

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
### Gerador

```sh
python3 generator/molecula_gen.py <schema.toml|schema.json> [-o <dir>]
```

| Arquivo gerado | Conteúdo |
|---|---|
| `Entities.hpp` | inclui todos os outros — é o único que a aplicação precisa incluir |
| `DatabaseBootstrap.hpp` | `syncAll()`: cria e migra todas as tabelas |
| `<Nome>Entity.hpp` | a entidade, com getters/setters e a definição de colunas |
| `<Nome>Repository.hpp` | o repositório, com os métodos declarados no schema |

## Limitações conhecidas

Estão aqui porque uma biblioteca que esconde o que não faz custa mais caro
depois:

- **Sem `JOIN`** no construtor de consultas. Chave estrangeira é declarada e
  criada no banco, mas a leitura de relacionamento é sua (ou SQL direto).
- **`BLOB` volta como `std::string`.** O conteúdo é preservado byte a byte, mas
  `DbValue` ainda não tem variante binária, então na volta não se distingue de
  `TEXT`.
- **Leitura que falha devolve vetor vazio.** Use `ok()` para diferenciar de "não
  achou".
- **Uma conexão por thread.** Não há pool.
- **Sem cache de *statements*.** Cada chamada prepara o SQL de novo. Para o
  volume de um aplicativo embarcado é barato; para carga alta, não.
- **Só SQLite vem na caixa.** A interface é agnóstica e outro banco é uma classe,
  mas não escrevemos nenhuma.

## Versão e compatibilidade

`0.x`: a API ainda pode mudar entre versões menores, e as mudanças ficam no
[CHANGELOG](CHANGELOG.md). A `1.0` sai quando a API tiver sido usada por alguém
além de quem a escreveu — promessa de estabilidade se faz depois do uso, não antes.

Requisitos: C++17, CMake 3.16+, SQLite 3.29+ (para `strictIdentifiers`; sem isso
a opção é ignorada). Testado em GCC e Clang no Linux.

## Contribuindo

Erro, ideia ou dúvida: abra uma *issue*. Para mudança de código, o que a CI
cobra — e é bom cobrar antes de mandar:

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

Defeito acompanhado de teste que falha antes e passa depois entra muito mais
rápido. A suíte roda com ASan, UBSan, TSan e em fuso não-UTC; se a sua mudança
mexe com tempo, concorrência ou SQL, ela vai encostar em pelo menos um desses.

## Licença

MIT — ver [LICENSE](LICENSE).
