#include <gtest/gtest.h>
#include <MoleculaEntity/SchemaManager.hpp>
#include <MoleculaEntity/SQLite3DatabaseManager.hpp>
#include "TestEntities.hpp"
#include "TestRepositories.hpp"
#include <cstdlib>
#include <ctime>

using namespace MoleculaEntity;
using namespace MoleculaEntity::Test;

class RepositoryTest : public ::testing::Test {
protected:
    std::shared_ptr<SQLite3DatabaseManager> db;
    std::unique_ptr<SchemaManager> schemaManager;
    std::unique_ptr<UserRepository> userRepo;
    std::unique_ptr<OrderRepository> orderRepo;

    void SetUp() override {
        db = SQLite3DatabaseManager::open(":memory:");
        ASSERT_TRUE(db) << "não abriu o banco em memória";
        schemaManager = std::make_unique<SchemaManager>(db);
        schemaManager->initialize();
        schemaManager->syncEntity<UserEntity>();
        schemaManager->syncEntity<OrderEntity>();

        userRepo = std::make_unique<UserRepository>(db);
        orderRepo = std::make_unique<OrderRepository>(db);
    }

    void TearDown() override {
        orderRepo.reset();
        userRepo.reset();
        schemaManager.reset();
        db.reset();
    }

    UserEntity createTestUser(const std::string& name, const std::string& email, int age = 25, bool active = true) {
        UserEntity user;
        user.setName(name);
        user.setEmail(email);
        user.setAge(age);
        user.setActive(active);
        return user;
    }
};

// Save Tests

TEST_F(RepositoryTest, SaveNewEntity) {
    auto user = createTestUser("John Doe", "john@example.com");

    auto savedUser = userRepo->save(user);

    EXPECT_TRUE(savedUser.isPersisted());
    EXPECT_TRUE(savedUser.getIdx().has_value());
    EXPECT_FALSE(savedUser.getId().empty());
    EXPECT_EQ(savedUser.getName(), "John Doe");
    EXPECT_EQ(savedUser.getEmail(), "john@example.com");
}

TEST_F(RepositoryTest, SaveGeneratesUuid) {
    auto user = createTestUser("Test User", "test@example.com");

    EXPECT_TRUE(user.getId().empty());

    auto savedUser = userRepo->save(user);

    EXPECT_FALSE(savedUser.getId().empty());
    EXPECT_EQ(savedUser.getId().length(), 36); // UUID format
}

TEST_F(RepositoryTest, SaveWithExistingUuid) {
    auto user = createTestUser("Test User", "test@example.com");
    user.setId("custom-uuid-12345");

    auto savedUser = userRepo->save(user);

    EXPECT_EQ(savedUser.getId(), "custom-uuid-12345");
}

TEST_F(RepositoryTest, SaveMultipleEntities) {
    auto user1 = userRepo->save(createTestUser("User 1", "user1@example.com"));
    auto user2 = userRepo->save(createTestUser("User 2", "user2@example.com"));
    auto user3 = userRepo->save(createTestUser("User 3", "user3@example.com"));

    EXPECT_EQ(user1.getIdx().value(), 1);
    EXPECT_EQ(user2.getIdx().value(), 2);
    EXPECT_EQ(user3.getIdx().value(), 3);
}

TEST_F(RepositoryTest, SaveWithNullableField) {
    UserEntity user;
    user.setName("No Age User");
    user.setEmail("noage@example.com");
    // age is not set

    auto savedUser = userRepo->save(user);

    EXPECT_FALSE(savedUser.getAge().has_value());
}

// Update Tests

TEST_F(RepositoryTest, UpdateExistingEntity) {
    auto user = userRepo->save(createTestUser("Original Name", "original@example.com"));

    user.setName("Updated Name");
    user.setEmail("updated@example.com");

    auto updatedUser = userRepo->save(user);

    EXPECT_EQ(updatedUser.getIdx(), user.getIdx());
    EXPECT_EQ(updatedUser.getName(), "Updated Name");
    EXPECT_EQ(updatedUser.getEmail(), "updated@example.com");
}

TEST_F(RepositoryTest, UpdateThrowsForNonPersisted) {
    auto user = createTestUser("Test", "test@example.com");

    EXPECT_THROW(userRepo->update(user), std::runtime_error);
}

// FindOne Tests

TEST_F(RepositoryTest, FindOneByIdx) {
    auto savedUser = userRepo->save(createTestUser("Find Me", "findme@example.com"));

    auto foundUser = userRepo->findOne(savedUser.getIdx().value());

    EXPECT_TRUE(foundUser.has_value());
    EXPECT_EQ(foundUser->getName(), "Find Me");
    EXPECT_EQ(foundUser->getEmail(), "findme@example.com");
}

TEST_F(RepositoryTest, FindOneReturnsNulloptForNonExistent) {
    auto foundUser = userRepo->findOne(999);

    EXPECT_FALSE(foundUser.has_value());
}

TEST_F(RepositoryTest, FindByUuid) {
    auto user = createTestUser("UUID User", "uuid@example.com");
    user.setId("my-unique-uuid");
    auto savedUser = userRepo->save(user);

    auto foundUser = userRepo->findByUuid("my-unique-uuid");

    EXPECT_TRUE(foundUser.has_value());
    EXPECT_EQ(foundUser->getName(), "UUID User");
}

TEST_F(RepositoryTest, FindByUuidReturnsNulloptForNonExistent) {
    auto foundUser = userRepo->findByUuid("non-existent-uuid");

    EXPECT_FALSE(foundUser.has_value());
}

// Find Tests

TEST_F(RepositoryTest, FindAll) {
    userRepo->save(createTestUser("User 1", "user1@example.com"));
    userRepo->save(createTestUser("User 2", "user2@example.com"));
    userRepo->save(createTestUser("User 3", "user3@example.com"));

    auto users = userRepo->findAll();

    EXPECT_EQ(users.size(), 3);
}

TEST_F(RepositoryTest, FindWithWhereClause) {
    userRepo->save(createTestUser("Active User", "active@example.com", 25, true));
    userRepo->save(createTestUser("Inactive User", "inactive@example.com", 30, false));

    QueryBuilder qb;
    qb.where("active", CompareOp::Equals, static_cast<int64_t>(1));

    auto activeUsers = userRepo->find(qb);

    EXPECT_EQ(activeUsers.size(), 1);
    EXPECT_EQ(activeUsers[0].getName(), "Active User");
}

TEST_F(RepositoryTest, FindWithMultipleConditions) {
    userRepo->save(createTestUser("Young Active", "young@example.com", 20, true));
    userRepo->save(createTestUser("Old Active", "old@example.com", 50, true));
    userRepo->save(createTestUser("Young Inactive", "younginactive@example.com", 22, false));

    QueryBuilder qb;
    qb.where("active", CompareOp::Equals, static_cast<int64_t>(1))
      .andWhere("age", CompareOp::LessThan, static_cast<int64_t>(30));

    auto users = userRepo->find(qb);

    EXPECT_EQ(users.size(), 1);
    EXPECT_EQ(users[0].getName(), "Young Active");
}

TEST_F(RepositoryTest, FindWithOrderBy) {
    userRepo->save(createTestUser("Charlie", "charlie@example.com", 30));
    userRepo->save(createTestUser("Alice", "alice@example.com", 25));
    userRepo->save(createTestUser("Bob", "bob@example.com", 35));

    QueryBuilder qb;
    qb.orderBy("name", OrderDirection::Asc);

    auto users = userRepo->find(qb);

    EXPECT_EQ(users.size(), 3);
    EXPECT_EQ(users[0].getName(), "Alice");
    EXPECT_EQ(users[1].getName(), "Bob");
    EXPECT_EQ(users[2].getName(), "Charlie");
}

TEST_F(RepositoryTest, FindWithLimit) {
    for (int i = 0; i < 10; ++i) {
        userRepo->save(createTestUser("User " + std::to_string(i), "user" + std::to_string(i) + "@example.com", i + 20));
    }

    QueryBuilder qb;
    qb.limit(5);

    auto users = userRepo->find(qb);

    EXPECT_EQ(users.size(), 5);
}

TEST_F(RepositoryTest, FindWithLimitAndOffset) {
    for (int i = 0; i < 10; ++i) {
        userRepo->save(createTestUser("User " + std::to_string(i), "user" + std::to_string(i) + "@example.com", i + 20));
    }

    QueryBuilder qb;
    qb.orderBy("idx", OrderDirection::Asc).limit(3).offset(3);

    auto users = userRepo->find(qb);

    EXPECT_EQ(users.size(), 3);
    EXPECT_EQ(users[0].getIdx().value(), 4);
}

TEST_F(RepositoryTest, FindByEmail) {
    userRepo->save(createTestUser("Test User", "specific@example.com"));
    userRepo->save(createTestUser("Other User", "other@example.com"));

    auto user = userRepo->findByEmail("specific@example.com");

    EXPECT_TRUE(user.has_value());
    EXPECT_EQ(user->getName(), "Test User");
}

TEST_F(RepositoryTest, FindActiveUsers) {
    userRepo->save(createTestUser("Active 1", "active1@example.com", 25, true));
    userRepo->save(createTestUser("Active 2", "active2@example.com", 30, true));
    userRepo->save(createTestUser("Inactive", "inactive@example.com", 35, false));

    auto activeUsers = userRepo->findActiveUsers();

    EXPECT_EQ(activeUsers.size(), 2);
}

TEST_F(RepositoryTest, FindByAgeRange) {
    userRepo->save(createTestUser("Too Young", "young@example.com", 15));
    userRepo->save(createTestUser("In Range 1", "range1@example.com", 25));
    userRepo->save(createTestUser("In Range 2", "range2@example.com", 30));
    userRepo->save(createTestUser("Too Old", "old@example.com", 50));

    auto usersInRange = userRepo->findByAgeRange(20, 35);

    EXPECT_EQ(usersInRange.size(), 2);
}

// Remove Tests

TEST_F(RepositoryTest, RemoveEntity) {
    auto user = userRepo->save(createTestUser("To Delete", "delete@example.com"));
    auto idx = user.getIdx().value();

    bool removed = userRepo->remove(user);

    EXPECT_TRUE(removed);
    EXPECT_FALSE(userRepo->findOne(idx).has_value());
}

TEST_F(RepositoryTest, RemoveById) {
    auto user = userRepo->save(createTestUser("To Delete", "delete@example.com"));
    auto idx = user.getIdx().value();

    bool removed = userRepo->removeById(idx);

    EXPECT_TRUE(removed);
    EXPECT_FALSE(userRepo->findOne(idx).has_value());
}

TEST_F(RepositoryTest, RemoveByUuid) {
    auto user = createTestUser("To Delete", "delete@example.com");
    user.setId("delete-uuid");
    userRepo->save(user);

    bool removed = userRepo->removeByUuid("delete-uuid");

    EXPECT_TRUE(removed);
    EXPECT_FALSE(userRepo->findByUuid("delete-uuid").has_value());
}

TEST_F(RepositoryTest, RemoveNonPersistedReturnsFalse) {
    auto user = createTestUser("Not Saved", "notsaved@example.com");

    bool removed = userRepo->remove(user);

    EXPECT_FALSE(removed);
}

// Count and Exists Tests

TEST_F(RepositoryTest, Count) {
    userRepo->save(createTestUser("User 1", "user1@example.com"));
    userRepo->save(createTestUser("User 2", "user2@example.com"));
    userRepo->save(createTestUser("User 3", "user3@example.com"));

    int64_t count = userRepo->count();

    EXPECT_EQ(count, 3);
}

TEST_F(RepositoryTest, CountWithCondition) {
    userRepo->save(createTestUser("Active 1", "active1@example.com", 25, true));
    userRepo->save(createTestUser("Active 2", "active2@example.com", 30, true));
    userRepo->save(createTestUser("Inactive", "inactive@example.com", 35, false));

    QueryBuilder qb;
    qb.where("active", CompareOp::Equals, static_cast<int64_t>(1));

    int64_t count = userRepo->count(qb);

    EXPECT_EQ(count, 2);
}

TEST_F(RepositoryTest, ExistsById) {
    auto user = userRepo->save(createTestUser("Exists", "exists@example.com"));

    EXPECT_TRUE(userRepo->existsById(user.getIdx().value()));
    EXPECT_FALSE(userRepo->existsById(999));
}

TEST_F(RepositoryTest, ExistsByUuid) {
    auto user = createTestUser("Exists", "exists@example.com");
    user.setId("exists-uuid");
    userRepo->save(user);

    EXPECT_TRUE(userRepo->existsByUuid("exists-uuid"));
    EXPECT_FALSE(userRepo->existsByUuid("non-existent-uuid"));
}

TEST_F(RepositoryTest, ExistsWithCondition) {
    userRepo->save(createTestUser("John", "john@example.com", 25, true));

    QueryBuilder qb;
    qb.where("email", CompareOp::Equals, std::string("john@example.com"));

    EXPECT_TRUE(userRepo->exists(qb));

    QueryBuilder qb2;
    qb2.where("email", CompareOp::Equals, std::string("nonexistent@example.com"));

    EXPECT_FALSE(userRepo->exists(qb2));
}

// Order Repository Tests

TEST_F(RepositoryTest, SaveOrder) {
    auto user = userRepo->save(createTestUser("User", "user@example.com"));

    OrderEntity order;
    order.setUserId(user.getIdx().value());
    order.setAmount(99.99);
    order.setStatus("pending");

    auto savedOrder = orderRepo->save(order);

    EXPECT_TRUE(savedOrder.isPersisted());
    EXPECT_EQ(savedOrder.getUserId(), user.getIdx().value());
    EXPECT_EQ(savedOrder.getAmount(), 99.99);
    EXPECT_EQ(savedOrder.getStatus(), "pending");
}

TEST_F(RepositoryTest, FindOrdersByUserId) {
    auto user = userRepo->save(createTestUser("User", "user@example.com"));

    OrderEntity order1;
    order1.setUserId(user.getIdx().value());
    order1.setAmount(50.0);
    order1.setStatus("pending");
    orderRepo->save(order1);

    OrderEntity order2;
    order2.setUserId(user.getIdx().value());
    order2.setAmount(75.0);
    order2.setStatus("completed");
    orderRepo->save(order2);

    auto orders = orderRepo->findByUserId(user.getIdx().value());

    EXPECT_EQ(orders.size(), 2);
}

TEST_F(RepositoryTest, FindOrdersByStatus) {
    auto user = userRepo->save(createTestUser("User", "user@example.com"));

    OrderEntity order1;
    order1.setUserId(user.getIdx().value());
    order1.setAmount(50.0);
    order1.setStatus("pending");
    orderRepo->save(order1);

    OrderEntity order2;
    order2.setUserId(user.getIdx().value());
    order2.setAmount(75.0);
    order2.setStatus("completed");
    orderRepo->save(order2);

    auto pendingOrders = orderRepo->findByStatus("pending");
    EXPECT_EQ(pendingOrders.size(), 1);
    EXPECT_EQ(pendingOrders[0].getStatus(), "pending");
}

TEST_F(RepositoryTest, FindOrdersByAmountGreaterThan) {
    auto user = userRepo->save(createTestUser("User", "user@example.com"));

    OrderEntity order1;
    order1.setUserId(user.getIdx().value());
    order1.setAmount(50.0);
    order1.setStatus("pending");
    orderRepo->save(order1);

    OrderEntity order2;
    order2.setUserId(user.getIdx().value());
    order2.setAmount(150.0);
    order2.setStatus("pending");
    orderRepo->save(order2);

    auto largeOrders = orderRepo->findByAmountGreaterThan(100.0);

    EXPECT_EQ(largeOrders.size(), 1);
    EXPECT_EQ(largeOrders[0].getAmount(), 150.0);
}

// Timestamp Tests

TEST_F(RepositoryTest, CreatedAtIsSet) {
    auto user = userRepo->save(createTestUser("Test", "test@example.com"));

    auto foundUser = userRepo->findOne(user.getIdx().value());

    EXPECT_TRUE(foundUser.has_value());
    EXPECT_TRUE(foundUser->getCreatedAt().has_value());
}

TEST_F(RepositoryTest, UpdatedAtIsSetOnCreate) {
    auto user = userRepo->save(createTestUser("Test", "test@example.com"));

    auto foundUser = userRepo->findOne(user.getIdx().value());

    EXPECT_TRUE(foundUser.has_value());
    EXPECT_TRUE(foundUser->getUpdatedAt().has_value());
}

// Complex Query Tests

TEST_F(RepositoryTest, ComplexQueryWithMultipleOperators) {
    userRepo->save(createTestUser("Alice", "alice@gmail.com", 25, true));
    userRepo->save(createTestUser("Bob", "bob@yahoo.com", 30, true));
    userRepo->save(createTestUser("Charlie", "charlie@gmail.com", 35, false));
    userRepo->save(createTestUser("Diana", "diana@gmail.com", 28, true));

    QueryBuilder qb;
    qb.where("email", CompareOp::Like, std::string("%@gmail.com"))
      .andWhere("active", CompareOp::Equals, static_cast<int64_t>(1))
      .andWhere("age", CompareOp::LessThan, static_cast<int64_t>(30))
      .orderBy("name", OrderDirection::Asc);

    auto users = userRepo->find(qb);

    EXPECT_EQ(users.size(), 2);
    EXPECT_EQ(users[0].getName(), "Alice");
    EXPECT_EQ(users[1].getName(), "Diana");
}

TEST_F(RepositoryTest, InOperator) {
    userRepo->save(createTestUser("Alice", "alice@example.com", 25, true));
    userRepo->save(createTestUser("Bob", "bob@example.com", 30, true));
    userRepo->save(createTestUser("Charlie", "charlie@example.com", 35, true));

    std::vector<DbValue> names = {std::string("Alice"), std::string("Charlie")};

    QueryBuilder qb;
    qb.whereIn("name", names);

    auto users = userRepo->find(qb);

    EXPECT_EQ(users.size(), 2);
}

// ---------------------------------------------------------------------------
// Regressões dos defeitos consertados na 0.1.0
// ---------------------------------------------------------------------------

// O `created_at` é preenchido pelo `CURRENT_TIMESTAMP` do SQLite, que grava em
// UTC. A leitura usava std::mktime, que interpreta hora LOCAL: num host em
// UTC−3 todo carimbo voltava três horas no passado. O teste falhava em São
// Paulo e passava num CI em UTC, que é o que manteve o defeito vivo — por isso
// ele fixa o fuso antes de medir.
TEST_F(RepositoryTest, TimestampsComeBackInUtcRegardlessOfTimezone) {
    const char* original = std::getenv("TZ");
    setenv("TZ", "America/Sao_Paulo", 1);
    tzset();

    const auto antes = std::chrono::system_clock::now();
    auto user = userRepo->save(createTestUser("Fuso", "fuso@exemplo.com"));
    const auto depois = std::chrono::system_clock::now();

    if (original != nullptr) { setenv("TZ", original, 1); } else { unsetenv("TZ"); }
    tzset();

    ASSERT_TRUE(user.getCreatedAt().has_value());

    const auto carimbo = user.getCreatedAt().value();

    // Um segundo de folga de cada lado: o SQLite grava com resolução de
    // segundo, e o relógio pode ter virado entre o `antes` e a inserção.
    EXPECT_GE(carimbo, antes - std::chrono::seconds(1));
    EXPECT_LE(carimbo, depois + std::chrono::seconds(1));
}

// Coluna cujo nome é palavra reservada do SQL. Sem aspas, o CREATE TABLE e todo
// SELECT em cima dela são erro de sintaxe; com aspas, é uma coluna como outra.
TEST_F(RepositoryTest, QuotedIdentifiersAllowReservedWords) {
    ASSERT_TRUE(db->execute("CREATE TABLE \"grupo\" (\"idx\" INTEGER PRIMARY KEY, \"order\" TEXT)"));
    ASSERT_TRUE(db->execute("INSERT INTO \"grupo\" (\"order\") VALUES (?)",
                            {DbValue{std::string("primeiro")}}));

    QueryBuilder qb;
    qb.where("order", CompareOp::Equals, DbValue{std::string("primeiro")});

    const auto linhas = db->query("SELECT \"order\" FROM \"grupo\"" + qb.buildFullClause(),
                                  qb.getParams());

    ASSERT_TRUE(db->ok()) << db->lastError();
    ASSERT_EQ(linhas.size(), 1u);
    EXPECT_EQ(std::get<std::string>(linhas[0][0]), "primeiro");
}

// O driver não lança: erro vira `false`, `ok()` falso e uma mensagem. Sem o
// `ok()`, consulta que falha e consulta sem resultado são as duas um vetor
// vazio, e quem chama não tem como distinguir "não tem" de "não deu".
TEST_F(RepositoryTest, DriverReportsErrorsWithoutThrowing) {
    EXPECT_FALSE(db->execute("ISTO NAO E SQL"));
    EXPECT_FALSE(db->ok());
    EXPECT_FALSE(db->lastError().empty());

    const auto linhas = db->query("SELECT * FROM tabela_que_nao_existe");
    EXPECT_TRUE(linhas.empty());
    EXPECT_FALSE(db->ok());

    // E volta ao normal no primeiro comando que dá certo.
    EXPECT_TRUE(db->execute("SELECT 1"));
    EXPECT_TRUE(db->ok());
    EXPECT_TRUE(db->lastError().empty());
}

// Pular linhas sem limitar quantas: o SQL tem que continuar válido de verdade,
// não só bem formado no construtor.
TEST_F(RepositoryTest, OffsetWithoutLimitRunsOnTheDatabase) {
    for (int i = 0; i < 5; ++i) {
        userRepo->save(createTestUser("U" + std::to_string(i), "u" + std::to_string(i) + "@exemplo.com"));
    }

    QueryBuilder qb;
    qb.orderBy("idx").offset(2);

    const auto encontrados = userRepo->find(qb);

    EXPECT_TRUE(db->ok()) << db->lastError();
    EXPECT_EQ(encontrados.size(), 3u);
}

// A citação não é cosmética: com o nome de coluna entrando cru, este `WHERE`
// viraria `nome = 'x' OR 1=1 --` e devolveria a tabela inteira. Citado, o banco
// recusa — que é o comportamento certo para um nome de coluna que não existe.
TEST_F(RepositoryTest, InjectedColumnNameIsRejectedByTheDatabase) {
    userRepo->save(createTestUser("Alice", "alice@exemplo.com"));
    userRepo->save(createTestUser("Bob", "bob@exemplo.com"));

    QueryBuilder qb;
    qb.where("name = 'Alice' OR 1=1 --", CompareOp::Equals, DbValue{std::string("x")});

    const auto encontrados = userRepo->find(qb);

    EXPECT_TRUE(encontrados.empty());
    EXPECT_FALSE(db->ok()) << "o banco tinha que recusar a coluna inexistente";
    EXPECT_NE(db->lastError().find("no such column"), std::string::npos) << db->lastError();
}
