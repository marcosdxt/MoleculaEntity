cmake_minimum_required(VERSION 3.14)
project(MoleculaEntity VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Option to build tests
option(MOLECULA_ENTITY_BUILD_TESTS "Build MoleculaEntity tests" ON)

# Header-only library
add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Tests
if(MOLECULA_ENTITY_BUILD_TESTS)
    enable_testing()

    find_package(SQLite3 REQUIRED)

    add_executable(${PROJECT_NAME}_tests
        tests/main.cpp
        tests/BaseEntityTest.cpp
        tests/QueryBuilderTest.cpp
        tests/SchemaManagerTest.cpp
        tests/RepositoryTest.cpp
        tests/UuidGeneratorTest.cpp
        tests/GeneratorTest.cpp
    )

    target_include_directories(${PROJECT_NAME}_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/generated_test
    )

    target_link_libraries(${PROJECT_NAME}_tests PRIVATE
        ${PROJECT_NAME}
        gtest
        gtest_main
        SQLite::SQLite3
    )

    add_test(NAME ${PROJECT_NAME}_tests COMMAND ${PROJECT_NAME}_tests)

    # Enable verbose test output
    set_property(TEST ${PROJECT_NAME}_tests PROPERTY
        ENVIRONMENT "GTEST_COLOR=1"
    )
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Targets
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}Targets.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)
