#include <gtest/gtest.h>

#include <MoleculaEntity/Identifier.hpp>
#include <MoleculaEntity/QueryBuilder.hpp>

using namespace MoleculaEntity;

TEST(Identifier, QuotesPlainName)
{
    EXPECT_EQ(quoteIdentifier("nome"), "\"nome\"");
}

TEST(Identifier, DoublesEmbeddedQuotes)
{
    // É isto que impede fechar as aspas e continuar escrevendo SQL.
    EXPECT_EQ(quoteIdentifier("a\"b"), "\"a\"\"b\"");
}

TEST(Identifier, QuotesEachPartOfQualifiedName)
{
    // "pedido"."total", e não "pedido.total" — que seria uma coluna só, com um
    // ponto no nome.
    EXPECT_EQ(quoteIdentifier("pedido.total"), "\"pedido\".\"total\"");
}

TEST(Identifier, ReservedWordSurvives)
{
    // `order` sem aspas é erro de sintaxe; com aspas é uma coluna como outra.
    EXPECT_EQ(quoteIdentifier("order"), "\"order\"");
}

// O defeito que as aspas existem para fechar: nome de coluna vindo de fora
// (uma ordenação escolhida na interface, um filtro de configuração) entrava
// cru no SQL, e a biblioteca tinha injeção por um caminho que ninguém olhava
// porque "os valores estão parametrizados".
TEST(Identifier, ColumnNameCannotEscapeIntoSql)
{
    QueryBuilder qb;
    qb.where("nome = 'x' OR 1=1 --", CompareOp::Equals, DbValue{std::string("y")});

    // O texto inteiro vira UM identificador entre aspas — inclusive o `--`, que
    // fora delas comentaria o resto da consulta. Quem prova que isso não é
    // apenas cosmético é RepositoryTest.InjectedColumnNameIsRejectedByTheDatabase:
    // lá o banco recusa a coluna, em vez de devolver a tabela inteira.
    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"nome = 'x' OR 1=1 --\" = ?");
}
