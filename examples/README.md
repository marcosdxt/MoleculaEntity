# Exemplos

Seis programas, do mais simples ao que você provavelmente vai precisar em
produção. Todos rodam de verdade — **são parte da suíte**, e o `ctest` executa
cada um deles.

```sh
cmake -S . -B build
cmake --build build --target exemplos
ctest --test-dir build -R exemplo --output-on-failure   # roda todos

./build/examples/exemplo-01                             # ou um de cada vez
```

| | Exemplo | O que mostra |
|---|---|---|
| 01 | [`01-basico`](01-basico/main.cpp) | Uma entidade e um repositório **escritos à mão**, e o CRUD inteiro. É o contrato que a biblioteca pede |
| 02 | [`02-gerador`](02-gerador/) | O mesmo domínio do 01, gerado de um `schema.toml`. ~130 linhas de C++ viram 20 de TOML |
| 03 | [`03-consultas`](03-consultas/) | O `QueryBuilder` inteiro — igualdade, `IN`, `LIKE`, `BETWEEN`, nulo, ordenação, paginação, contagem — **e onde ele termina** |
| 04 | [`04-migracoes`](04-migracoes/main.cpp) | O banco que já existe no campo: versão 1 instalada com dados, versão 2 acrescentando coluna sem perder nada |
| 05 | [`05-erros-e-transacoes`](05-erros-e-transacoes/main.cpp) | Erro sem exceção, `ok()` × vetor vazio, transação com rollback, e as opções do driver que importam em disco |
| 06 | [`06-driver-proprio`](06-driver-proprio/main.cpp) | Implementando `IDatabaseManager`: um decorador que mede cada SQL. É como se descobre por que uma tela está lenta |

## Por onde começar

**Nunca usou:** 01 e depois 02, nessa ordem. O 01 existe para você ver o que o
gerador está preenchendo — sem isso, o 02 parece mágica, e mágica é difícil de
depurar quando quebra.

**Já usa e quer filtrar melhor:** 03.

**Vai colocar num serviço:** 05 primeiro (é lá que estão as opções que decidem
se o banco sobrevive a uma queda de energia), depois 04.

**Está investigando lentidão:** 06. O relatório dele mostra, por exemplo, que
cada `save()` faz um `SELECT` a mais para devolver a linha como ela ficou:

```
   50 x     648 us  INSERT INTO "produtos" ("id", "nome", "preco") VALUES (?, ?, ?)
   50 x     561 us  SELECT * FROM "produtos" WHERE "idx" = ?
```

## Detalhes que valem para todos

- **`:memory:` na maioria.** Só o 04 grava arquivo, porque "atualizar um banco
  que já existe" não faz sentido em memória. Ele apaga o que criou ao terminar.
- **Nenhum exemplo monta SQL concatenando texto.** Valores vão por `?` e
  identificadores vão citados — inclusive nas consultas cruas do 03 e do 05.
- **Código gerado não está versionado.** Os exemplos 02 e 03 têm `schema.toml`; o
  C++ correspondente nasce no diretório de build, durante a compilação.
