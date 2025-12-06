#include <gtest/gtest.h>
#include <MoleculaEntity/UuidGenerator.hpp>
#include <set>
#include <regex>

using namespace MoleculaEntity;

class UuidGeneratorTest : public ::testing::Test {};

TEST_F(UuidGeneratorTest, GeneratesValidUuidFormat) {
    std::string uuid = UuidGenerator::generate();

    // UUID format: 8-4-4-4-12 = 36 characters
    EXPECT_EQ(uuid.length(), 36);

    // Check format with regex
    std::regex uuidPattern("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
    EXPECT_TRUE(std::regex_match(uuid, uuidPattern)) << "Invalid UUID format: " << uuid;
}

TEST_F(UuidGeneratorTest, GeneratesUniqueUuids) {
    std::set<std::string> uuids;
    const int count = 1000;

    for (int i = 0; i < count; ++i) {
        uuids.insert(UuidGenerator::generate());
    }

    EXPECT_EQ(uuids.size(), count) << "Generated duplicate UUIDs";
}

TEST_F(UuidGeneratorTest, IsVersion4Uuid) {
    for (int i = 0; i < 100; ++i) {
        std::string uuid = UuidGenerator::generate();
        // Version 4 UUIDs have '4' at position 14
        EXPECT_EQ(uuid[14], '4') << "UUID is not version 4: " << uuid;
    }
}

TEST_F(UuidGeneratorTest, HasCorrectVariant) {
    for (int i = 0; i < 100; ++i) {
        std::string uuid = UuidGenerator::generate();
        // Variant bits should be 10xx (8, 9, a, or b)
        char variantChar = uuid[19];
        EXPECT_TRUE(variantChar == '8' || variantChar == '9' ||
                    variantChar == 'a' || variantChar == 'b')
            << "UUID has incorrect variant: " << uuid;
    }
}

TEST_F(UuidGeneratorTest, ContainsOnlyValidCharacters) {
    std::string uuid = UuidGenerator::generate();

    for (size_t i = 0; i < uuid.length(); ++i) {
        char c = uuid[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            EXPECT_EQ(c, '-') << "Expected hyphen at position " << i;
        } else {
            EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
                << "Invalid character '" << c << "' at position " << i;
        }
    }
}
