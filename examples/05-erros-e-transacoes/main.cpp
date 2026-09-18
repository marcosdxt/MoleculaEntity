// 05 — Erro sem exceção, transação, e as opções que importam em disco.
//
// Este é o exemplo para quem vai rodar isto num serviço: o que acontece quando
// o banco recusa, como desfazer um bloco inteiro, e o que os padrões do driver
// protegem.

#include <MoleculaEntity/MoleculaEntity.hpp>
#include <MoleculaEntity/SQLite3DatabaseManager.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace MoleculaEntity;

int main()
{
    const std::string caminho = "exemplo-05.db";
    std::remove(caminho.c_str());

    // ------------------------------------------------------ as opções ---
    // Os padrões já são os de um serviço que grava em disco. Estão explícitos
    // aqui para mostrar o que cada um evita:
    SQLite3DatabaseManager::Options opcoes;
    opcoes.walJournal = true;                                  // leitor não trava escritor
    opcoes.busyTimeout = std::chrono::seconds(5);              // o padrão do SQLite é ZERO
    opcoes.synchronous = SQLite3DatabaseManager::Options::Synchronous::Full;
    opcoes.foreignKeys = true;                                 // o padrão do SQLite é desligado
    opcoes.strictIdentifiers = true;                           // "texto" não vira string à toa

    std::string erro;
    auto db = SQLite3DatabaseManager::open(caminho, opcoes, &erro);
    if (!db) {
        // Abrir devolve nulo em vez de lançar. Num serviço, isto vira log e
        // uma decisão — não uma pilha desenrolada.
        std::fprintf(stderr, "não abriu: %s\n", erro.c_str());
        return 1;
    }
    std::printf("aberto com WAL e busy_timeout de %lld ms\n",
                static_cast<long long>(opcoes.busyTimeout.count()));

    db->execute("CREATE TABLE contas (idx INTEGER PRIMARY KEY, dono TEXT UNIQUE, saldo INTEGER NOT NULL)");
    db->execute("INSERT INTO contas (dono, saldo) VALUES (?, ?)", {std::string{"ana"},  int64_t{100}});
    db->execute("INSERT INTO contas (dono, saldo) VALUES (?, ?)", {std::string{"bruno"}, int64_t{ 50}});

    // ---------------------------------------------- erro não é exceção ---
    if (!db->execute("INSERT INTO contas (dono, saldo) VALUES (?, ?)",
                     {std::string{"ana"}, int64_t{999}})) {
        std::printf("recusado, como esperado: %s\n", db->lastError().c_str());
    }

    // ------------------------------------------- vazio ≠ deu errado ---
    // As duas consultas abaixo devolvem zero linhas. Só o ok() separa "não tem"
    // de "não deu" — é para isso que ele existe.
    const auto semResultado = db->query("SELECT * FROM contas WHERE dono = ?", {std::string{"ninguém"}});
    std::printf("consulta válida sem resultado: %zu linhas, ok=%s\n",
                semResultado.size(), db->ok() ? "sim" : "não");

    const auto comErro = db->query("SELECT * FROM tabela_que_nao_existe");
    std::printf("consulta inválida:              %zu linhas, ok=%s (%s)\n",
                comErro.size(), db->ok() ? "sim" : "não", db->lastError().c_str());

    // ----------------------------------------------------- transação ---
    // Transferência: ou as duas pontas mudam, ou nenhuma. Sem transação, uma
    // falha no meio deixa dinheiro sumido.
    const auto transferir = [&db](const char* de, const char* para, int64_t valor) {
        db->beginTransaction();

        const auto saldo = db->query("SELECT saldo FROM contas WHERE dono = ?", {std::string{de}});
        if (saldo.empty() || std::get<int64_t>(saldo[0][0]) < valor) {
            db->rollback();
            std::printf("transferência de %lld recusada: saldo insuficiente em %s\n",
                        static_cast<long long>(valor), de);
            return false;
        }

        const bool ok =
            db->execute("UPDATE contas SET saldo = saldo - ? WHERE dono = ?", {valor, std::string{de}}) &&
            db->execute("UPDATE contas SET saldo = saldo + ? WHERE dono = ?", {valor, std::string{para}});

        if (!ok) {
            db->rollback();
            std::printf("transferência desfeita: %s\n", db->lastError().c_str());
            return false;
        }

        db->commit();
        std::printf("transferidos %lld de %s para %s\n",
                    static_cast<long long>(valor), de, para);
        return true;
    };

    transferir("ana", "bruno", 30);
    transferir("bruno", "ana", 500);   // recusada, e o rollback devolve tudo

    for (const auto& l : db->query("SELECT dono, saldo FROM contas ORDER BY dono")) {
        std::printf("  %s: %lld\n", std::get<std::string>(l[0]).c_str(),
                    static_cast<long long>(std::get<int64_t>(l[1])));
    }

    // -------------------------------------------- chave estrangeira ---
    // Ligada por padrão. Sem isso, o SQLite ACEITA a linha órfã em silêncio, e o
    // defeito só aparece meses depois, na leitura.
    db->execute("CREATE TABLE lancamentos (idx INTEGER PRIMARY KEY, conta_idx INTEGER NOT NULL, "
                "valor INTEGER NOT NULL, FOREIGN KEY (conta_idx) REFERENCES contas(idx))");

    if (!db->execute("INSERT INTO lancamentos (conta_idx, valor) VALUES (?, ?)",
                     {int64_t{9999}, int64_t{10}})) {
        std::printf("órfã recusada pela chave estrangeira: %s\n", db->lastError().c_str());
    }

    std::remove(caminho.c_str());
    std::remove((caminho + "-wal").c_str());
    std::remove((caminho + "-shm").c_str());
    return 0;
}
