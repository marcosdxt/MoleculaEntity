# MoleculaEntity v2 - Proposta de Refatoração

## Objetivo

Simplificar o MoleculaEntity mantendo sua essência de ORM leve para sistemas embarcados, eliminando redundâncias e tornando-o mais próximo do estilo TypeORM.

---

## Arquitetura Proposta

```
┌─────────────────────────────────────────────────────────────┐
│                    MOLECULAENTITY v2                        │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌───────────────────┐     ┌─────────────────────────────┐  │
│  │  schema.json      │────▶│  molecula_gen.py            │  │
│  │  (definição)      │     │  (gerador de código)        │  │
│  └───────────────────┘     └─────────────────────────────┘  │
│                                      │                      │
│                                      ▼                      │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Código Gerado (por entidade em diretório próprio)   │   │
│  │  ├── checkout/                                       │   │
│  │  │   └── CheckoutEntity.hpp                          │   │
│  │  ├── checkout-step/                                  │   │
│  │  │   └── CheckoutStepEntity.hpp                      │   │
│  │  └── Entities.hpp     (include agregador)            │   │
│  └──────────────────────────────────────────────────────┘   │
│                                      │                      │
│                                      ▼                      │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Runtime Library (Header-Only)                       │   │
│  │  ├── Provider         (registro de DB + entidades)   │   │
│  │  ├── Repository<T>    (CRUD genérico, 1 classe)      │   │
│  │  ├── QueryBuilder     (DSL para queries)             │   │
│  │  └── IDatabaseDriver  (interface do driver)          │   │
│  └──────────────────────────────────────────────────────┘   │
│                                      │                      │
│                                      ▼                      │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Driver Concreto (fornecido pela aplicação)          │   │
│  │  └── SQLite3Driver.hpp                               │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Mudanças Principais

### 1. Eliminar Repositórios Gerados

**Antes (v1):** Cada entidade gera um `*Repository.hpp` com código duplicado.

**Depois (v2):** Um único `Repository<TEntity>` genérico que usa metadados da entidade.

```cpp
// v1 - CheckoutRepository.hpp gerado (100+ linhas cada)
class CheckoutRepository : public BaseRepository<CheckoutEntity> {
    std::vector<std::string> getInsertColumns(...) override { ... }
    std::vector<DbValue> getInsertValues(...) override { ... }
    CheckoutEntity mapRowToEntity(...) override { ... }
};

// v2 - Repository genérico único
auto& checkoutRepo = provider.getRepository<CheckoutEntity>();
checkoutRepo.save(entity);
```

### 2. Provider Central

Nova classe que centraliza:
- Registro do driver de banco
- Registro de entidades
- Sincronização de schema (CREATE IF NOT EXISTS + FK constraints)
- Acesso aos repositórios

```cpp
// Inicialização
MoleculaEntity::Provider provider;
provider.setDriver(std::make_shared<SQLite3Driver>("app.db"));
provider.registerEntities<CheckoutEntity, CheckoutStepEntity>();
provider.synchronize();  // Cria/atualiza tabelas com FK

// Uso
auto& repo = provider.getRepository<CheckoutEntity>();
```

### 3. Entidades Auto-Descritivas

Entidades geradas expõem metadados que o Repository usa para operações:

```cpp
class CheckoutEntity : public BaseEntity {
public:
    // Metadados (gerados)
    static constexpr auto tableName = "checkout";
    static constexpr auto tableVersion = 1;

    static const std::vector<ColumnMeta>& columns() {
        static std::vector<ColumnMeta> cols = {
            {"value", ColumnType::Int64, false},
            {"state", ColumnType::String, false},
            {"external_ref", ColumnType::String, true},
        };
        return cols;
    }

    // Serialização (gerados)
    std::vector<DbValue> toValues() const;
    static CheckoutEntity fromRow(const DbRow& row);

    // Getters/Setters (gerados)
    int64_t getValue() const { return value_; }
    void setValue(int64_t v) { value_ = v; }
    // ...
};
```

### 4. Renomear Interfaces

| v1                  | v2                |
|---------------------|-------------------|
| `IDatabaseManager`  | `IDatabaseDriver` |
| `DatabaseBootstrap` | `Provider`        |
| `SchemaManager`     | (interno ao Provider) |

---

## Foreign Keys e Relations

### Declaração no Schema

- **Filho:** declara `fk` na coluna que referencia o pai
- **Pai:** declara `relations` com array de filhos que pode carregar

```json
{
  "entities": {
    "Checkout": {
      "table": "checkout",
      "columns": {
        "value": { "type": "int64" },
        "state": { "type": "string" }
      },
      "relations": [
        {
          "name": "steps",
          "entity": "CheckoutStep",
          "fkColumn": "checkout_id"
        }
      ]
    },
    "CheckoutStep": {
      "table": "checkout_step",
      "columns": {
        "checkout_id": {
          "type": "string",
          "fk": {
            "entity": "Checkout",
            "column": "id",
            "onDelete": "CASCADE"
          }
        },
        "step_type": { "type": "string" },
        "status": { "type": "string" }
      }
    }
  }
}
```

### Opções de FK (no filho)

| Campo | Tipo | Default | Descrição |
|-------|------|---------|-----------|
| `entity` | string | obrigatório | Nome da entidade referenciada |
| `column` | string | `"id"` | Coluna referenciada (geralmente `id` ou `idx`) |
| `onDelete` | string | `"NO ACTION"` | `CASCADE`, `SET NULL`, `RESTRICT`, `NO ACTION` |
| `onUpdate` | string | `"NO ACTION"` | `CASCADE`, `SET NULL`, `RESTRICT`, `NO ACTION` |

### Opções de Relations (no pai)

| Campo | Tipo | Default | Descrição |
|-------|------|---------|-----------|
| `name` | string | obrigatório | Nome do getter (ex: `"steps"` → `getSteps()`) |
| `entity` | string | obrigatório | Nome da entidade filha |
| `fkColumn` | string | obrigatório | Coluna FK na entidade filha |

### SQL Gerado

```sql
CREATE TABLE checkout_step (
    idx INTEGER PRIMARY KEY AUTOINCREMENT,
    id TEXT UNIQUE NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    checkout_id TEXT NOT NULL,
    step_type TEXT NOT NULL,
    status TEXT NOT NULL,
    FOREIGN KEY (checkout_id) REFERENCES checkout(id) ON DELETE CASCADE ON UPDATE CASCADE
);

CREATE INDEX idx_checkout_step_checkout_id ON checkout_step(checkout_id);
```

### Código Gerado - Entidade Pai (com relations)

```cpp
// checkout/CheckoutEntity.hpp
class CheckoutEntity : public MoleculaEntity::BaseEntity {
public:
    static constexpr const char* tableName = "checkout";

    static const std::vector<MoleculaEntity::ColumnMeta>& columns() {
        static std::vector<MoleculaEntity::ColumnMeta> cols = {
            {"value", ColumnType::Int64, false},
            {"state", ColumnType::String, false},
        };
        return cols;
    }

    // Relation: steps (gerado a partir de relations[])
    const std::vector<CheckoutStepEntity>& getSteps() const { return steps_; }
    void setSteps(std::vector<CheckoutStepEntity> steps) { steps_ = std::move(steps); }

    // Getters/Setters das colunas
    int64_t getValue() const { return value_; }
    void setValue(int64_t v) { value_ = v; }

    const std::string& getState() const { return state_; }
    void setState(const std::string& v) { state_ = v; }

private:
    int64_t value_{0};
    std::string state_;
    std::vector<CheckoutStepEntity> steps_;  // populado via setSteps()
};
```

### Código Gerado - Entidade Filha (com fk)

```cpp
// checkout-step/CheckoutStepEntity.hpp
class CheckoutStepEntity : public MoleculaEntity::BaseEntity {
public:
    static constexpr const char* tableName = "checkout_step";

    static const std::vector<MoleculaEntity::ColumnMeta>& columns() {
        static std::vector<MoleculaEntity::ColumnMeta> cols = {
            {"checkout_id", ColumnType::String, false, ForeignKey{"checkout", "id", "CASCADE"}},
            {"step_type", ColumnType::String, false},
            {"status", ColumnType::String, false},
        };
        return cols;
    }

    // FK column
    const std::string& getCheckoutId() const { return checkout_id_; }
    void setCheckoutId(const std::string& v) { checkout_id_ = v; }

    const std::string& getStepType() const { return step_type_; }
    void setStepType(const std::string& v) { step_type_ = v; }

    const std::string& getStatus() const { return status_; }
    void setStatus(const std::string& v) { status_ = v; }

private:
    std::string checkout_id_;
    std::string step_type_;
    std::string status_;
};
```

### Uso

```cpp
// Salvar checkout
EzPay::CheckoutEntity checkout;
checkout.setValue(10000);
checkout.setState("waiting");
checkoutRepo.save(checkout);

// Salvar step com FK
EzPay::CheckoutStepEntity step;
step.setCheckoutId(checkout.getId());  // FK simples
step.setStepType("payment");
step.setStatus("pending");
stepRepo.save(step);

// Carregar steps e popular no checkout
QueryBuilder qb;
qb.where("checkout_id", Op::Eq, checkout.getId());
auto steps = stepRepo.find(qb);
checkout.setSteps(steps);

// Agora checkout.getSteps() retorna os steps
for (const auto& s : checkout.getSteps()) {
    std::cout << s.getStepType() << ": " << s.getStatus() << std::endl;
}

// Buscar checkout de um step (via FK)
auto parentCheckout = checkoutRepo.findByUuid(step.getCheckoutId());
```

---

## Análise de Complexidade O(n)

### Operações CRUD Básicas

| Operação | Complexidade | Queries SQL |
|----------|--------------|-------------|
| `save()` (insert) | O(1) | 1 INSERT |
| `save()` (update) | O(1) | 1 UPDATE |
| `remove()` | O(1) | 1 DELETE |
| `findByIdx()` | O(1) | 1 SELECT (PK index) |
| `findByUuid()` | O(1) | 1 SELECT (unique index) |
| `findAll()` | O(n) | 1 SELECT |
| `find(QueryBuilder)` | O(n) | 1 SELECT |
| `count()` | O(1) | 1 COUNT |

### Operações com FK (Busca Manual)

| Operação | Complexidade | Queries SQL | Notas |
|----------|--------------|-------------|-------|
| Buscar parent por FK | O(1) | 1 | `findByUuid(step.getCheckoutId())` |
| Buscar filhos por FK | O(m) | 1 | `find(qb.where("checkout_id", ...))` |
| Buscar filhos em batch | O(k*m) | 1 | `WHERE checkout_id IN (?, ?, ...)` |

### Exemplo de Busca com FK

```cpp
// Buscar checkout e seus steps (2 queries - simples e explícito)
auto checkout = checkoutRepo.findByUuid("abc-123");  // 1 query

QueryBuilder qb;
qb.where("checkout_id", Op::Eq, checkout->getId());
auto steps = stepRepo.find(qb);  // 1 query

// Batch: buscar steps de múltiplos checkouts (evita N+1)
auto checkouts = checkoutRepo.findAll();  // 1 query

std::vector<std::string> ids;
for (const auto& c : checkouts) {
    ids.push_back(c.getId());
}

QueryBuilder qb;
qb.whereIn("checkout_id", ids);
auto allSteps = stepRepo.find(qb);  // 1 query com IN clause
```

### Tabela Resumo de Complexidade

```
┌─────────────────────────────────────────────────────────────────┐
│                    COMPLEXIDADE POR OPERAÇÃO                    │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  n = número de registros na tabela                              │
│  m = número de registros relacionados (filhos)                  │
│  k = número de entidades no batch                               │
│                                                                 │
│  ┌────────────────────┬──────────┬─────────┬──────────────────┐ │
│  │ Operação           │ Tempo    │ Queries │ Memória          │ │
│  ├────────────────────┼──────────┼─────────┼──────────────────┤ │
│  │ insert             │ O(1)     │ 1       │ O(1)             │ │
│  │ update             │ O(1)     │ 1       │ O(1)             │ │
│  │ delete             │ O(1)     │ 1       │ O(1)             │ │
│  │ findByPK           │ O(1)     │ 1       │ O(1)             │ │
│  │ findAll            │ O(n)     │ 1       │ O(n)             │ │
│  │ find(query)        │ O(n)     │ 1       │ O(resultado)     │ │
│  │ find children (FK) │ O(m)     │ 1       │ O(m)             │ │
│  │ find children batch│ O(k*m)   │ 1       │ O(k*m)           │ │
│  └────────────────────┴──────────┴─────────┴──────────────────┘ │
│                                                                 │
│  Índices assumidos:                                             │
│  - PK: idx (INTEGER PRIMARY KEY) - B-tree O(log n)              │
│  - Unique: id (UUID) - B-tree O(log n)                          │
│  - FK: checkout_id - índice criado automaticamente              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Estrutura de Diretórios Gerados

```
src/models/                          # output_dir do schema.json
├── checkout/                        # lower-case-com-hifen
│   └── CheckoutEntity.hpp
├── checkout-step/
│   └── CheckoutStepEntity.hpp
├── user/
│   └── UserEntity.hpp
└── Entities.hpp                     # include agregador
```

### Entities.hpp Gerado

```cpp
#pragma once

#include <MoleculaEntity/MoleculaEntity.hpp>

#include "checkout/CheckoutEntity.hpp"
#include "checkout-step/CheckoutStepEntity.hpp"
#include "user/UserEntity.hpp"
```

---

## Estrutura de Arquivos da Biblioteca

```
components/MoleculaEntity/
├── include/MoleculaEntity/
│   ├── MoleculaEntity.hpp      # Include principal
│   ├── BaseEntity.hpp          # Classe base (mantida)
│   ├── Repository.hpp          # Repository<T> genérico (NOVO)
│   ├── Provider.hpp            # Registro central (NOVO)
│   ├── IDatabaseDriver.hpp     # Interface do driver (renomeado)
│   ├── QueryBuilder.hpp        # DSL queries (mantido)
│   ├── ColumnMeta.hpp          # Metadados de coluna (NOVO)
│   ├── RelationMeta.hpp        # Metadados de relacionamento (NOVO)
│   └── UuidGenerator.hpp       # UUID v4 (mantido)
│
├── drivers/
│   └── SQLite3Driver.hpp       # Implementação SQLite3
│
├── generator/
│   ├── molecula_gen.py         # Gerador simplificado
│   └── schema.example.json     # Schema de exemplo
│
└── tests/
    ├── ProviderTest.cpp
    ├── RepositoryTest.cpp
    ├── RelationsTest.cpp
    ├── QueryBuilderTest.cpp
    └── ...
```

---

## Schema JSON Completo

```json
{
  "config": {
    "namespace": "EzPay",
    "output_dir": "src/models"
  },
  "entities": {
    "Checkout": {
      "table": "checkout",
      "version": 1,
      "columns": {
        "value": { "type": "int64" },
        "state": { "type": "string" },
        "external_ref": { "type": "string", "nullable": true },
        "metadata": { "type": "string", "nullable": true }
      },
      "relations": [
        {
          "name": "steps",
          "entity": "CheckoutStep",
          "fkColumn": "checkout_id"
        }
      ],
      "indexes": [
        { "columns": ["state"] },
        { "columns": ["external_ref"], "unique": true }
      ]
    },
    "CheckoutStep": {
      "table": "checkout_step",
      "version": 1,
      "columns": {
        "checkout_id": {
          "type": "string",
          "fk": {
            "entity": "Checkout",
            "column": "id",
            "onDelete": "CASCADE"
          }
        },
        "step_type": { "type": "string" },
        "status": { "type": "string" },
        "request_payload": { "type": "string", "nullable": true },
        "response_payload": { "type": "string", "nullable": true }
      },
      "indexes": [
        { "columns": ["checkout_id", "step_type"] }
      ]
    }
  }
}
```

---

## Uso na Aplicação (main.cpp)

```cpp
#include <MoleculaEntity/MoleculaEntity.hpp>
#include <MoleculaEntity/drivers/SQLite3Driver.hpp>
#include "models/Entities.hpp"

int main() {
    // 1. Configurar provider
    MoleculaEntity::Provider provider;
    provider.setDriver(std::make_shared<MoleculaEntity::SQLite3Driver>("ezpay.db"));

    // 2. Registrar entidades (ordem importa para FK)
    provider.registerEntities<
        EzPay::CheckoutEntity,
        EzPay::CheckoutStepEntity
    >();

    // 3. Sincronizar schema (CREATE TABLE IF NOT EXISTS + FK)
    provider.synchronize();

    // 4. Usar repositórios
    auto& checkoutRepo = provider.getRepository<EzPay::CheckoutEntity>();
    auto& stepRepo = provider.getRepository<EzPay::CheckoutStepEntity>();

    // CREATE parent
    EzPay::CheckoutEntity checkout;
    checkout.setValue(10000);
    checkout.setState("waiting");
    checkoutRepo.save(checkout);

    // CREATE child com FK
    EzPay::CheckoutStepEntity step;
    step.setCheckoutId(checkout.getId());  // FK simples
    step.setStepType("payment");
    step.setStatus("pending");
    stepRepo.save(step);

    // Carregar filhos manualmente
    auto found = checkoutRepo.findByUuid(checkout.getId());

    MoleculaEntity::QueryBuilder qb;
    qb.where("checkout_id", MoleculaEntity::Op::Eq, found->getId());
    auto steps = stepRepo.find(qb);
    found->setSteps(steps);  // popula a relation

    for (const auto& s : found->getSteps()) {
        std::cout << s.getStepType() << ": " << s.getStatus() << std::endl;
    }

    return 0;
}
```

---

## Plano de Testes

### Cobertura Mínima para 100%

```
tests/
├── unit/
│   ├── BaseEntityTest.cpp          # idx, id, timestamps
│   ├── ColumnMetaTest.cpp          # tipos, nullable, default, fk
│   ├── QueryBuilderTest.cpp        # where, whereIn, orderBy, limit
│   ├── UuidGeneratorTest.cpp       # formato, unicidade
│   └── ProviderTest.cpp            # registro, sync
│
├── integration/
│   ├── RepositoryTest.cpp          # CRUD completo
│   │   ├── test_save_insert
│   │   ├── test_save_update
│   │   ├── test_remove
│   │   ├── test_findByIdx
│   │   ├── test_findByUuid
│   │   ├── test_findAll
│   │   ├── test_find_with_query
│   │   ├── test_find_with_whereIn
│   │   ├── test_count
│   │   └── test_exists
│   │
│   ├── ForeignKeyTest.cpp          # FK constraints
│   │   ├── test_fk_save_child
│   │   ├── test_fk_constraint_violation
│   │   ├── test_fk_on_delete_cascade
│   │   ├── test_fk_on_delete_set_null
│   │   ├── test_fk_query_children
│   │   └── test_fk_batch_query_children
│   │
│   ├── SchemaTest.cpp              # DDL
│   │   ├── test_create_table
│   │   ├── test_create_with_fk
│   │   ├── test_create_indexes
│   │   ├── test_create_fk_index_auto
│   │   ├── test_sync_idempotent
│   │   └── test_migration
│   │
│   └── TransactionTest.cpp         # ACID
│       ├── test_commit
│       ├── test_rollback
│       └── test_nested_transaction
│
├── performance/
│   ├── BulkInsertTest.cpp          # 1000, 10000 registros
│   ├── QueryPerformanceTest.cpp    # índices, full scan
│   └── FkQueryTest.cpp             # FK queries com/sem índice
│
└── generator/
    ├── test_generate_entity.py
    ├── test_generate_fk.py
    ├── test_generate_relations.py
    ├── test_output_structure.py
    └── test_schema_validation.py
```

### Métricas de Cobertura

| Módulo | Linhas | Branches | Target |
|--------|--------|----------|--------|
| BaseEntity | 100% | 100% | OK |
| Repository | 100% | 95% | OK |
| Provider | 100% | 100% | OK |
| QueryBuilder | 100% | 100% | OK |
| ColumnMeta/FK | 100% | 100% | OK |
| SQLite3Driver | 95% | 90% | OK |
| Generator | 100% | 100% | OK |

---

## Comparativo de Complexidade

| Aspecto | v1 | v2 |
|---------|----|----|
| Arquivos gerados por entidade | 2 (Entity + Repository) | 1 (Entity) |
| Linhas por entidade gerada | ~150 | ~80 |
| Classes runtime | 5 | 4 |
| Suporte a FK | Manual | Automático (via schema) |
| Suporte a relations | Não | Via `relations[]` no pai |
| Índice FK | Manual | Criado automaticamente |
| Duplicação de código | Alta | Baixa |
| Flexibilidade de queries | Via herança | Via QueryBuilder |

---

## Plano de Implementação

### Fase 1: Core Runtime
1. Criar `ColumnMeta.hpp` com tipos, nullable, default e FK
2. Criar `Provider.hpp` com registro e sync (inclui FK constraints)
3. Refatorar `Repository.hpp` para ser genérico
4. Renomear `IDatabaseManager` → `IDatabaseDriver`
5. **Testes:** ProviderTest, RepositoryTest básico

### Fase 2: Foreign Keys
1. Implementar FK constraints no CREATE TABLE
2. Criar índice automático em colunas FK
3. Suporte a onDelete/onUpdate
4. **Testes:** ForeignKeyTest completo

### Fase 3: Driver SQLite3
1. Mover para `drivers/SQLite3Driver.hpp`
2. Adaptar para nova interface
3. **Testes:** Todos os testes de integração

### Fase 4: Gerador
1. Refatorar `molecula_gen.py` para novo formato JSON
2. Gerar Entity com `toValues()`, `fromRow()`
3. Gerar `relations[]` como `getX()` / `setX()` no pai
4. Gerar estrutura de diretórios `lower-case-com-hifen/`
5. **Testes:** Generator tests em Python

### Fase 5: Migração ez-pay-gateway
1. Regenerar entidades com novo formato
2. Atualizar `main.cpp` para usar Provider
3. Remover código legado

---

## Próximos Passos

Aguardando aprovação para iniciar implementação da Fase 1.
