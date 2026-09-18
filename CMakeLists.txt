cmake_minimum_required(VERSION 3.16)

# 0.x, e não 1.0: a API ainda muda. A versão 1.0 é uma promessa de
# compatibilidade, e ela se faz depois de a biblioteca ter sido usada por
# alguém que não seja quem a escreveu.
project(MoleculaEntity
    VERSION 0.1.0
    DESCRIPTION "Entity/Repository em C++17 para SQLite, com gerador de código"
    HOMEPAGE_URL "https://github.com/marcosdxt/MoleculaEntity"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Testes ligados só quando este é o projeto de cima. Consumido por
# add_subdirectory ou FetchContent, a suíte de outro projeto não é problema de
# quem está construindo.
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

# GNUInstallDirs antes dos alvos: é ele que define CMAKE_INSTALL_INCLUDEDIR, e
# usar a variável antes deixa o `$<INSTALL_INTERFACE:>` vazio — a biblioteca
# instala os cabeçalhos e quem a consome não acha nenhum.
include(GNUInstallDirs)

# Resolvido uma vez, usado pelos testes e pelos exemplos. O gerador é opcional:
# sem Python, o que depende dele fica de fora com um aviso, e o resto continua.
find_package(Python3 COMPONENTS Interpreter QUIET)
set(MOLECULA_GENERATOR ${CMAKE_CURRENT_SOURCE_DIR}/generator/molecula_gen.py)

# ------------------------------------------------------------ a biblioteca ---
# Só cabeçalhos, e sem nenhuma dependência: quem usa outro banco implementa
# IDatabaseManager e não linka nada.
add_library(MoleculaEntity INTERFACE)
add_library(MoleculaEntity::MoleculaEntity ALIAS MoleculaEntity)

target_compile_features(MoleculaEntity INTERFACE cxx_std_17)
target_include_directories(MoleculaEntity INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# ------------------------------------------------------ o driver de SQLite ---
# Alvo separado porque é ele — e só ele — que obriga a linkar SQLite. Quem quer
# o ORM sem SQLite linka MoleculaEntity::MoleculaEntity e pronto.
if(MOLECULA_BUILD_SQLITE3)
    find_package(SQLite3 REQUIRED)

    add_library(MoleculaEntity_SQLite3 INTERFACE)
    add_library(MoleculaEntity::SQLite3 ALIAS MoleculaEntity_SQLite3)

    # Sem o EXPORT_NAME, quem instala e faz find_package recebe
    # `MoleculaEntity::MoleculaEntity_SQLite3` — que não é o nome escrito em
    # lugar nenhum da documentação, e o alias local não é exportado junto.
    set_target_properties(MoleculaEntity_SQLite3 PROPERTIES EXPORT_NAME SQLite3)

    target_link_libraries(MoleculaEntity_SQLite3 INTERFACE
        MoleculaEntity
        SQLite::SQLite3)
endif()

# ---------------------------------------------------------------- exemplos ---
if(MOLECULA_BUILD_EXAMPLES)
    enable_testing()
    add_subdirectory(examples)
endif()

# ------------------------------------------------------------------ testes ---
if(MOLECULA_BUILD_TESTS)
    enable_testing()

    # GoogleTest do sistema quando existe; baixado quando não. Sem o segundo
    # caminho, `git clone && cmake && ctest` não funciona em máquina limpa — e
    # era assim que este repositório estava.
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        message(STATUS "GoogleTest não encontrado no sistema; buscando via FetchContent")
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

    # O gerador roda no BUILD, e é isto que conserta o clone limpo: os arquivos
    # gerados são ignorados pelo git (e devem ser — são derivados), então até
    # aqui o GeneratorTest incluía um cabeçalho que nunca existia e o projeto
    # recém-clonado não compilava.
    if(Python3_Interpreter_FOUND)
        set(MOLECULA_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_test)
        set(MOLECULA_TEST_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/generator/schema.example.toml)

        add_custom_command(
            OUTPUT ${MOLECULA_GENERATED_DIR}/Entities.hpp
            COMMAND ${CMAKE_COMMAND} -E make_directory ${MOLECULA_GENERATED_DIR}
            COMMAND Python3::Interpreter ${MOLECULA_GENERATOR}
                    ${MOLECULA_TEST_SCHEMA} -o ${MOLECULA_GENERATED_DIR}
            DEPENDS ${MOLECULA_GENERATOR} ${MOLECULA_TEST_SCHEMA}
            COMMENT "Gerando entidades e repositórios do schema de exemplo"
            VERBATIM)

        add_custom_target(molecula_generate DEPENDS ${MOLECULA_GENERATED_DIR}/Entities.hpp)

        list(APPEND MOLECULA_TEST_SOURCES tests/GeneratorTest.cpp)
    else()
        message(WARNING
            "Python3 não encontrado: o gerador não roda e os testes dele ficam de fora. "
            "O resto da suíte continua valendo.")
    endif()

    add_executable(MoleculaEntity_tests ${MOLECULA_TEST_SOURCES})

    target_link_libraries(MoleculaEntity_tests PRIVATE
        MoleculaEntity
        GTest::gtest_main)

    if(MOLECULA_BUILD_SQLITE3)
        target_link_libraries(MoleculaEntity_tests PRIVATE MoleculaEntity::SQLite3)
    else()
        message(FATAL_ERROR "Os testes exercitam o driver de SQLite: ligue MOLECULA_BUILD_SQLITE3.")
    endif()

    if(Python3_Interpreter_FOUND)
        add_dependencies(MoleculaEntity_tests molecula_generate)
        target_include_directories(MoleculaEntity_tests PRIVATE ${MOLECULA_GENERATED_DIR})
    endif()

    # Os avisos valem para o código da biblioteca, que é o que o teste inclui.
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(MoleculaEntity_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
    endif()

    gtest_discover_tests(MoleculaEntity_tests)
endif()

# --------------------------------------------------------------- instalação ---
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

# O Config precisa reencontrar o SQLite no lado de quem consome; sem isto,
# `find_package(MoleculaEntity)` exporta um alvo que referencia SQLite::SQLite3
# e falha na configuração do outro projeto.
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
