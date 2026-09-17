# Changelog

Formato: [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/).
Versões seguem [SemVer](https://semver.org/lang/pt-BR/) — em `0.x`, a API pode
mudar entre versões menores.

## [0.1.0] — 2026-09-17

Primeira versão publicável. O código já existia; o que esta versão faz é torná-lo
utilizável por quem não o escreveu — e consertar quatro defeitos que só aparecem
fora da bancada de quem escreveu.

### Corrigido

- **Carimbos de tempo voltavam deslocados pelo fuso da máquina.** O
  `CURRENT_TIMESTAMP` do SQLite grava em UTC; a leitura usava `std::mktime`, que
  interpreta hora local. Num host em UTC−3, todo `created_at`/`updated_at` voltava
  três horas no passado. A conversão agora é UTC dos dois lados e não consulta o
  fuso do sistema (`MoleculaEntity/Time.hpp`), e a suíte roda em fuso não-UTC na
  CI — antes o defeito era invisível justamente porque CI roda em UTC.
- **`offset()` sem `limit()` gerava SQL inválido.** `OFFSET` sozinho é erro de
  sintaxe em SQLite; agora sai `LIMIT -1 OFFSET n`.
- **Corrida de dados no gerador de uuid.** O motor `std::mt19937_64` era `static`
  e sem trava: duas threads gerando id ao mesmo tempo era comportamento
  indefinido, e na prática podia repetir id — que é coluna única. Agora é
  `thread_local`, confirmado sob ThreadSanitizer.
- **Nomes de tabela e coluna entravam crus no SQL.** Valores sempre foram
  parametrizados, mas identificadores eram concatenados: um nome vindo de
  configuração ou da interface escapava para o SQL. Agora são citados e escapados
  (`MoleculaEntity/Identifier.hpp`), o que de quebra faz funcionar coluna com nome
  de palavra reservada (`order`, `group`).
- **O projeto recém-clonado não compilava.** `GeneratorTest` incluía cabeçalhos
  que o gerador produz e que o `.gitignore` ignora, e nada chamava o gerador. O
  CMake agora o executa durante o build.
- **Código gerado disparava `-Wunused-parameter`** nos projetos que o incluíam.

### Adicionado

- **`SQLite3DatabaseManager` como parte da biblioteca.** Era uma *fixture* de
  teste, no namespace `::Test`: quem usasse a biblioteca precisava escrever o
  próprio driver. Agora é o alvo `MoleculaEntity::SQLite3`, e vem com WAL,
  `busy_timeout`, `synchronous`, `foreign_keys` e `strictIdentifiers`
  configuráveis — com padrões de serviço que grava em disco, não de teste.
- **Modelo de erro sem exceção** no driver: `execute` devolve `false`, e `ok()` e
  `lastError()` dizem o que houve. `ok()` existe para separar "a consulta falhou"
  de "a consulta não achou nada", que antes eram os dois um vetor vazio.
- `MoleculaEntity::time_utils::parseUtc` / `formatUtc`.
- `LICENSE` (MIT) — sem ela, ninguém podia usar isto legalmente.
- CI: GCC e Clang, ASan/UBSan/TSan, suíte em `America/Sao_Paulo` e `Asia/Tokyo`, e
  um job que instala a biblioteca e a consome de fora por `find_package`.
- `CMakePackageConfigHelpers`: `find_package(MoleculaEntity)` passa a funcionar.
- 18 testes novos, todos amarrados aos defeitos acima. Total: 135.
- **`examples/`**: seis exemplos, do CRUD à mão até um `IDatabaseManager`
  próprio que mede cada SQL. São executados pelo `ctest` (total: 141) — exemplo
  que não compila é pior que exemplo que não existe.
- **`assets/`**: logo (marca e lockup) e a visão de arquitetura em SVG, os dois
  com variante para tema escuro.

### Alterado — quebra compatibilidade

- `IDatabaseManager` ganhou `ok()` e `lastError()` (puros virtuais) e **perdeu
  `escapeString()`**. Quem implementa a interface precisa acrescentar os dois
  primeiros; o `escapeString` saiu porque nada o chamava e o que ele oferecia era
  o caminho para montar SQL concatenando texto — o oposto do que a biblioteca faz.
- `SQLite3DatabaseManager` abre por `open()`, que devolve nulo em vez de lançar, e
  mudou de `MoleculaEntity::Test` para `MoleculaEntity`.
- O SQL montado agora traz identificadores entre aspas. Testes que comparavam a
  string exata precisam ser atualizados.
- Versão do projeto: era `1.0.0` no CMake, passa a `0.1.0`. `1.0` é promessa de
  estabilidade, e ela se faz depois do uso.
- Alvos do CMake: `MoleculaEntity::MoleculaEntity` (só cabeçalhos) e
  `MoleculaEntity::SQLite3` (com o driver). A opção de teste virou
  `MOLECULA_BUILD_TESTS`.
