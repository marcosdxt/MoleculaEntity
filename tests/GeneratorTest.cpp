#include <gtest/gtest.h>
#include <MoleculaEntity/SchemaManager.hpp>
#include <MoleculaEntity/SQLite3DatabaseManager.hpp>

// Include generated files
// Gerado no build a partir de generator/schema.example.toml; o diretório entra
// por target_include_directories (ver CMakeLists.txt).
#include "Entities.hpp"

using namespace MoleculaEntity;
using namespace MyApp;

class GeneratorTest : public ::testing::Test {
protected:
    std::shared_ptr<MoleculaEntity::SQLite3DatabaseManager> db;
    std::unique_ptr<DatabaseBootstrap> bootstrap;

    void SetUp() override {
        db = MoleculaEntity::SQLite3DatabaseManager::open(":memory:");
        ASSERT_TRUE(db) << "could not open the in-memory database";
        bootstrap = std::make_unique<DatabaseBootstrap>(db);
        ASSERT_TRUE(bootstrap->syncAll()) << bootstrap->lastError();
    }

    void TearDown() override {
        bootstrap.reset();
        db.reset();
    }
};

TEST_F(GeneratorTest, UserEntityCreation) {
    UserEntity user;
    user.setName("John Doe");
    user.setEmail("john@example.com");
    user.setAge(30);
    user.setActive(true);
    user.setSalary(50000.0);

    EXPECT_EQ(user.getName(), "John Doe");
    EXPECT_EQ(user.getEmail(), "john@example.com");
    EXPECT_EQ(user.getAge().value(), 30);
    EXPECT_TRUE(user.isActive());
    EXPECT_EQ(user.getSalary().value(), 50000.0);
}

TEST_F(GeneratorTest, UserRepositorySave) {
    auto& userRepo = bootstrap->userRepository();

    UserEntity user;
    user.setName("Jane Doe");
    user.setEmail("jane@example.com");
    user.setAge(25);

    auto savedUser = userRepo.save(user);

    EXPECT_TRUE(savedUser.isPersisted());
    EXPECT_FALSE(savedUser.getId().empty());
    EXPECT_EQ(savedUser.getName(), "Jane Doe");
}

TEST_F(GeneratorTest, UserRepositoryFindByEmail) {
    auto& userRepo = bootstrap->userRepository();

    UserEntity user;
    user.setName("Test User");
    user.setEmail("test@example.com");
    userRepo.save(user);

    auto foundUser = userRepo.findByEmail("test@example.com");

    EXPECT_TRUE(foundUser.has_value());
    EXPECT_EQ(foundUser->getName(), "Test User");
}

TEST_F(GeneratorTest, UserRepositoryFindActiveUsers) {
    auto& userRepo = bootstrap->userRepository();

    UserEntity activeUser;
    activeUser.setName("Active");
    activeUser.setEmail("active@example.com");
    activeUser.setActive(true);
    userRepo.save(activeUser);

    UserEntity inactiveUser;
    inactiveUser.setName("Inactive");
    inactiveUser.setEmail("inactive@example.com");
    inactiveUser.setActive(false);
    userRepo.save(inactiveUser);

    auto activeUsers = userRepo.findActiveUsers();

    EXPECT_EQ(activeUsers.size(), 1);
    EXPECT_EQ(activeUsers[0].getName(), "Active");
}

TEST_F(GeneratorTest, UserRepositoryFindByAgeRange) {
    auto& userRepo = bootstrap->userRepository();

    UserEntity young;
    young.setName("Young");
    young.setEmail("young@example.com");
    young.setAge(20);
    userRepo.save(young);

    UserEntity middle;
    middle.setName("Middle");
    middle.setEmail("middle@example.com");
    middle.setAge(35);
    userRepo.save(middle);

    UserEntity old;
    old.setName("Old");
    old.setEmail("old@example.com");
    old.setAge(60);
    userRepo.save(old);

    auto usersInRange = userRepo.findByAgeRange(25, 40);

    EXPECT_EQ(usersInRange.size(), 1);
    EXPECT_EQ(usersInRange[0].getName(), "Middle");
}

TEST_F(GeneratorTest, OrderEntityWithForeignKey) {
    auto& userRepo = bootstrap->userRepository();
    auto& orderRepo = bootstrap->orderRepository();

    UserEntity user;
    user.setName("Customer");
    user.setEmail("customer@example.com");
    auto savedUser = userRepo.save(user);

    OrderEntity order;
    order.setUserId(savedUser.getIdx().value());
    order.setAmount(99.99);
    order.setStatus("pending");

    auto savedOrder = orderRepo.save(order);

    EXPECT_TRUE(savedOrder.isPersisted());
    EXPECT_EQ(savedOrder.getUserId(), savedUser.getIdx().value());
    EXPECT_EQ(savedOrder.getAmount(), 99.99);
}

TEST_F(GeneratorTest, OrderRepositoryFindByUserId) {
    auto& userRepo = bootstrap->userRepository();
    auto& orderRepo = bootstrap->orderRepository();

    UserEntity user;
    user.setName("Customer");
    user.setEmail("customer@example.com");
    auto savedUser = userRepo.save(user);

    OrderEntity order1;
    order1.setUserId(savedUser.getIdx().value());
    order1.setAmount(50.0);
    order1.setStatus("pending");
    orderRepo.save(order1);

    OrderEntity order2;
    order2.setUserId(savedUser.getIdx().value());
    order2.setAmount(75.0);
    order2.setStatus("completed");
    orderRepo.save(order2);

    auto orders = orderRepo.findByUserId(savedUser.getIdx().value());

    EXPECT_EQ(orders.size(), 2);
}

TEST_F(GeneratorTest, OrderRepositoryFindByStatus) {
    auto& userRepo = bootstrap->userRepository();
    auto& orderRepo = bootstrap->orderRepository();

    UserEntity user;
    user.setName("Customer");
    user.setEmail("customer@example.com");
    auto savedUser = userRepo.save(user);

    OrderEntity order1;
    order1.setUserId(savedUser.getIdx().value());
    order1.setAmount(50.0);
    order1.setStatus("pending");
    orderRepo.save(order1);

    OrderEntity order2;
    order2.setUserId(savedUser.getIdx().value());
    order2.setAmount(75.0);
    order2.setStatus("completed");
    orderRepo.save(order2);

    auto pendingOrders = orderRepo.findByStatus("pending");

    EXPECT_EQ(pendingOrders.size(), 1);
    EXPECT_EQ(pendingOrders[0].getStatus(), "pending");
}

TEST_F(GeneratorTest, ProductEntity) {
    auto& productRepo = bootstrap->productRepository();

    ProductEntity product;
    product.setName("Widget");
    product.setDescription("A useful widget");
    product.setPrice(29.99);
    product.setStock(100);
    product.setCategory("Electronics");

    auto savedProduct = productRepo.save(product);

    EXPECT_TRUE(savedProduct.isPersisted());
    EXPECT_EQ(savedProduct.getName(), "Widget");
    EXPECT_EQ(savedProduct.getPrice(), 29.99);
}

TEST_F(GeneratorTest, ProductRepositoryFindByCategory) {
    auto& productRepo = bootstrap->productRepository();

    ProductEntity electronics;
    electronics.setName("Phone");
    electronics.setPrice(999.0);
    electronics.setStock(50);
    electronics.setCategory("Electronics");
    productRepo.save(electronics);

    ProductEntity clothing;
    clothing.setName("Shirt");
    clothing.setPrice(29.99);
    clothing.setStock(100);
    clothing.setCategory("Clothing");
    productRepo.save(clothing);

    auto electronicsProducts = productRepo.findByCategory("Electronics");

    EXPECT_EQ(electronicsProducts.size(), 1);
    EXPECT_EQ(electronicsProducts[0].getName(), "Phone");
}

TEST_F(GeneratorTest, ProductRepositoryFindInStock) {
    auto& productRepo = bootstrap->productRepository();

    ProductEntity inStock;
    inStock.setName("Available");
    inStock.setPrice(10.0);
    inStock.setStock(50);
    productRepo.save(inStock);

    ProductEntity outOfStock;
    outOfStock.setName("Sold Out");
    outOfStock.setPrice(20.0);
    outOfStock.setStock(0);
    productRepo.save(outOfStock);

    auto availableProducts = productRepo.findInStock();

    EXPECT_EQ(availableProducts.size(), 1);
    EXPECT_EQ(availableProducts[0].getName(), "Available");
}

TEST_F(GeneratorTest, ProductRepositoryFindByPriceRange) {
    auto& productRepo = bootstrap->productRepository();

    ProductEntity cheap;
    cheap.setName("Cheap");
    cheap.setPrice(5.0);
    cheap.setStock(10);
    productRepo.save(cheap);

    ProductEntity medium;
    medium.setName("Medium");
    medium.setPrice(50.0);
    medium.setStock(10);
    productRepo.save(medium);

    ProductEntity expensive;
    expensive.setName("Expensive");
    expensive.setPrice(500.0);
    expensive.setStock(10);
    productRepo.save(expensive);

    auto midRangeProducts = productRepo.findByPriceRange(20.0, 100.0);

    EXPECT_EQ(midRangeProducts.size(), 1);
    EXPECT_EQ(midRangeProducts[0].getName(), "Medium");
}

TEST_F(GeneratorTest, SchemaSyncIdempotent) {
    // Sync should be idempotent - calling it again shouldn't cause issues
    EXPECT_TRUE(bootstrap->syncAll()) << bootstrap->lastError();
    EXPECT_TRUE(bootstrap->syncAll()) << bootstrap->lastError();

    auto& userRepo = bootstrap->userRepository();
    UserEntity user;
    user.setName("Test");
    user.setEmail("test@example.com");

    auto savedUser = userRepo.save(user);
    EXPECT_TRUE(savedUser.isPersisted());
}
