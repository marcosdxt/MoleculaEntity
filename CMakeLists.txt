cmake_minimum_required(VERSION 3.16)

# 0.x, not 1.0: the API can still change. Version 1.0 is a compatibility promise,
# and that is made after the library has been used by someone other than the
# person who wrote it.
project(MoleculaEntity
    VERSION 0.1.0
    DESCRIPTION "Entity/Repository for C++17 and SQLite, with a code generator"
    HOMEPAGE_URL "https://github.com/marcosdxt/MoleculaEntity"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Tests on only when this is the top-level project. Consumed through
# add_subdirectory or FetchContent, another project's suite is not the concern of
# whoever is building.
if(PROJECT_IS_TOP_LEVEL OR CMAKE_VERSION VERSION_LESS 3.21)
    option(MOLECULA_BUILD_TESTS "Build the MoleculaEntity test suite" ON)
else()
    option(MOLECULA_BUILD_TESTS "Build the MoleculaEntity test suite" OFF)
endif()

option(MOLECULA_BUILD_SQLITE3 "Build the bundled SQLite3 driver target" ON)

if(PROJECT_IS_TOP_LEVEL OR CMAKE_VERSION VERSION_LESS 3.21)
    option(MOLECULA_BUILD_EXAMPLES "Build the examples in examples/" ON)
else()
    option(MOLECULA_BUILD_EXAMPLES "Build the examples in examples/" OFF)
endif()

# GNUInstallDirs before the targets: it's what defines CMAKE_INSTALL_INCLUDEDIR,
# and using the variable earlier leaves `$<INSTALL_INTERFACE:>` empty — the library
# installs the headers and whoever consumes it finds none.
include(GNUInstallDirs)

# Resolved once, used by the tests and by the examples. The generator is optional:
# without Python, whatever depends on it is left out with a warning, and the rest
# carries on.
find_package(Python3 COMPONENTS Interpreter QUIET)
set(MOLECULA_GENERATOR ${CMAKE_CURRENT_SOURCE_DIR}/generator/molecula_gen.py)

# ------------------------------------------------------------- the library ---
# Headers only, with no dependency at all: anyone using another database
# implements IDatabaseManager and links nothing.
add_library(MoleculaEntity INTERFACE)
add_library(MoleculaEntity::MoleculaEntity ALIAS MoleculaEntity)

target_compile_features(MoleculaEntity INTERFACE cxx_std_17)
target_include_directories(MoleculaEntity INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# ------------------------------------------------------- the SQLite driver ---
# A separate target because it — and only it — forces linking SQLite. Anyone who
# wants the ORM without SQLite links MoleculaEntity::MoleculaEntity and is done.
if(MOLECULA_BUILD_SQLITE3)
    find_package(SQLite3 REQUIRED)

    add_library(MoleculaEntity_SQLite3 INTERFACE)
    add_library(MoleculaEntity::SQLite3 ALIAS MoleculaEntity_SQLite3)

    # Without EXPORT_NAME, whoever installs and runs find_package gets
    # `MoleculaEntity::MoleculaEntity_SQLite3` — which is not the name written
    # anywhere in the documentation, and the local alias is not exported with it.
    set_target_properties(MoleculaEntity_SQLite3 PROPERTIES EXPORT_NAME SQLite3)

    target_link_libraries(MoleculaEntity_SQLite3 INTERFACE
        MoleculaEntity
        SQLite::SQLite3)
endif()

# ---------------------------------------------------------------- examples ---
if(MOLECULA_BUILD_EXAMPLES)
    enable_testing()
    add_subdirectory(examples)
endif()

# ------------------------------------------------------------------- tests ---
if(MOLECULA_BUILD_TESTS)
    enable_testing()

    # The system GoogleTest when it exists; downloaded when it doesn't. Without
    # the second path, `git clone && cmake && ctest` doesn't work on a clean
    # machine — and that is how this repository used to be.
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        message(STATUS "GoogleTest not found on the system; fetching it with FetchContent")
        include(FetchContent)
        FetchContent_Declare(googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.15.2
            GIT_SHALLOW TRUE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    include(GoogleTest)

    set(MOLECULA_TEST_SOURCES
        tests/main.cpp
        tests/BaseEntityTest.cpp
        tests/QueryBuilderTest.cpp
        tests/IdentifierTest.cpp
        tests/TimeTest.cpp
        tests/SchemaManagerTest.cpp
        tests/MigrationFailureTest.cpp
        tests/RepositoryTest.cpp
        tests/UuidGeneratorTest.cpp)

    # The generator runs during the BUILD, and that is what fixes the fresh
    # clone: generated files are gitignored (and should be — they're derived), so
    # until now GeneratorTest included a header that never existed and a
    # freshly-cloned project didn't compile.
    if(Python3_Interpreter_FOUND)
        set(MOLECULA_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_test)
        set(MOLECULA_TEST_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/generator/schema.example.toml)

        add_custom_command(
            OUTPUT ${MOLECULA_GENERATED_DIR}/Entities.hpp
            COMMAND ${CMAKE_COMMAND} -E make_directory ${MOLECULA_GENERATED_DIR}
            COMMAND Python3::Interpreter ${MOLECULA_GENERATOR}
                    ${MOLECULA_TEST_SCHEMA} -o ${MOLECULA_GENERATED_DIR}
            DEPENDS ${MOLECULA_GENERATOR} ${MOLECULA_TEST_SCHEMA}
            COMMENT "Generating entities and repositories from the example schema"
            VERBATIM)

        add_custom_target(molecula_generate DEPENDS ${MOLECULA_GENERATED_DIR}/Entities.hpp)

        list(APPEND MOLECULA_TEST_SOURCES tests/GeneratorTest.cpp)
    else()
        message(WARNING
            "Python3 not found: the generator can't run and its tests are left out. "
            "The rest of the suite still applies.")
    endif()

    add_executable(MoleculaEntity_tests ${MOLECULA_TEST_SOURCES})

    target_link_libraries(MoleculaEntity_tests PRIVATE
        MoleculaEntity
        GTest::gtest_main)

    if(MOLECULA_BUILD_SQLITE3)
        target_link_libraries(MoleculaEntity_tests PRIVATE MoleculaEntity::SQLite3)
    else()
        message(FATAL_ERROR "The tests exercise the SQLite driver: turn MOLECULA_BUILD_SQLITE3 on.")
    endif()

    if(Python3_Interpreter_FOUND)
        add_dependencies(MoleculaEntity_tests molecula_generate)
        target_include_directories(MoleculaEntity_tests PRIVATE ${MOLECULA_GENERATED_DIR})
    endif()

    # The warnings apply to the library code, which is what the tests include.
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(MoleculaEntity_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
    endif()

    gtest_discover_tests(MoleculaEntity_tests)
endif()

# ------------------------------------------------------------- installation ---
install(TARGETS MoleculaEntity EXPORT MoleculaEntityTargets)
if(MOLECULA_BUILD_SQLITE3)
    install(TARGETS MoleculaEntity_SQLite3 EXPORT MoleculaEntityTargets)
endif()

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(PROGRAMS generator/molecula_gen.py
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME molecula-gen)

install(EXPORT MoleculaEntityTargets
    FILE MoleculaEntityTargets.cmake
    NAMESPACE MoleculaEntity::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MoleculaEntity)

include(CMakePackageConfigHelpers)

# The Config file has to find SQLite again on the consumer's side; without this,
# `find_package(MoleculaEntity)` exports a target referencing SQLite::SQLite3 and
# fails during the other project's configure step.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/MoleculaEntityConfig.cmake.in
"@PACKAGE_INIT@
include(CMakeFindDependencyMacro)
if(@MOLECULA_BUILD_SQLITE3@)
    find_dependency(SQLite3)
endif()
include(\"\${CMAKE_CURRENT_LIST_DIR}/MoleculaEntityTargets.cmake\")
check_required_components(MoleculaEntity)
")

configure_package_config_file(
    ${CMAKE_CURRENT_BINARY_DIR}/MoleculaEntityConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/MoleculaEntityConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MoleculaEntity)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/MoleculaEntityConfigVersion.cmake
    COMPATIBILITY SameMajorVersion)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/MoleculaEntityConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/MoleculaEntityConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MoleculaEntity)
