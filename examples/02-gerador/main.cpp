// 02 — O mesmo domínio do exemplo 01, pelo gerador.
//
// O exemplo 01 tem ~130 linhas de entidade e repositório escritos à mão. Aqui o
// equivalente são as 20 linhas de `schema.toml` ao lado, e o C++ abaixo só usa
// o que saiu delas.
//
// O que o gerador emitiu (em `gerado/`, dentro do diretório de build):
//
//     Entities.hpp           inclui todo o resto — é o único que você inclui
//     DatabaseBootstrap.hpp  syncAll(): cria e migra todas as tabelas
//     TarefaEntity.hpp       a entidade, com getters e setters tipados
//     TarefaRepository.hpp   o repositório, com os métodos do schema
//
// Código gerado não se versiona: é derivado do schema, como um .o é derivado do
// .cpp. Quem versiona os dois garante que um dia eles discordem.

#include <MoleculaEntity/SQLite3DatabaseManager.hpp>

#include "Entities.hpp"

#include <cstdio>

int main()
{
    auto db = MoleculaEntity::SQLite3DatabaseManager::open(":memory:");
    if (!db) {
        std::fprintf(stderr, "não abriu o banco\n");
        return 1;
    }

    // Uma linha no lugar do initialize() + syncEntity<T>() de cada entidade. Com
    // dez tabelas no schema, continua sendo uma linha.
    Agenda::DatabaseBootstrap bootstrap(db);
    if (!bootstrap.syncAll()) {
        std::fprintf(stderr, "esquema: %s\n", bootstrap.lastError().c_str());
        return 1;
    }

    Agenda::TarefaRepository tarefas(db);

    Agenda::TarefaEntity comprar;
    comprar.setTitulo("Comprar café");
    comprar.setPrazo("2026-09-20");
    comprar.setPrioridade(1);
    comprar = tarefas.save(comprar);

    Agenda::TarefaEntity revisar;
    revisar.setTitulo("Revisar o PR");
    revisar.setPrioridade(2);
    revisar = tarefas.save(revisar);

    Agenda::TarefaEntity arquivar;
    arquivar.setTitulo("Arquivar notas");
    arquivar.setPrioridade(5);
    arquivar.setConcluida(true);
    arquivar = tarefas.save(arquivar);

    // Os três métodos abaixo vieram da seção [entities.Tarefa.repository] do
    // schema — não foram escritos em C++ em lugar nenhum.
    if (auto achada = tarefas.buscarPorTitulo("Comprar café")) {
        std::printf("achei: %s (prioridade %d)\n",
                    achada->getTitulo().c_str(), achada->getPrioridade());
    }

    std::printf("pendentes: %lld\n", static_cast<long long>(tarefas.pendentes().size()));

    std::printf("prioridade 1..2:\n");
    for (const auto& t : tarefas.porPrioridade(1, 2)) {
        std::printf("  %s\n", t.getTitulo().c_str());
    }

    // O CRUD genérico continua ali: o gerador ACRESCENTA métodos ao
    // BaseRepository, não substitui o que ele já faz.
    std::printf("total: %lld\n", static_cast<long long>(tarefas.count()));

    return 0;
}
