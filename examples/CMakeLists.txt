# Os exemplos são construídos e EXECUTADOS pelo ctest.
#
# Exemplo que não compila é pior que exemplo que não existe: quem chega novo
# confia nele, perde uma tarde, e conclui que a biblioteca é que está quebrada.
# Amarrá-los à suíte é o que impede isso — quando a API mudar, eles quebram
# junto com os testes, e não meses depois no issue de alguém.

add_custom_target(exemplos)

# Gera entidades e repositórios de um schema, no diretório de build do exemplo.
function(molecula_exemplo_gerado alvo schema)
    set(destino ${CMAKE_CURRENT_BINARY_DIR}/${alvo}-gerado)

    add_custom_command(
        OUTPUT ${destino}/Entities.hpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${destino}
        COMMAND Python3::Interpreter ${MOLECULA_GENERATOR} ${schema} -o ${destino}
        DEPENDS ${MOLECULA_GENERATOR} ${schema}
        COMMENT "Gerando o schema do exemplo ${alvo}"
        VERBATIM)

    add_custom_target(${alvo}-gerar DEPENDS ${destino}/Entities.hpp)
    add_dependencies(${alvo} ${alvo}-gerar)
    target_include_directories(${alvo} PRIVATE ${destino})
endfunction()

function(molecula_exemplo alvo fonte)
    add_executable(${alvo} ${fonte})
    target_link_libraries(${alvo} PRIVATE MoleculaEntity::SQLite3)
    add_dependencies(exemplos ${alvo})

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${alvo} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    # O exemplo 04 grava arquivo no diretório de trabalho; cada um roda no seu.
    add_test(NAME ${alvo} COMMAND ${alvo} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endfunction()

molecula_exemplo(exemplo-01 01-basico/main.cpp)
molecula_exemplo(exemplo-04 04-migracoes/main.cpp)
molecula_exemplo(exemplo-05 05-erros-e-transacoes/main.cpp)
molecula_exemplo(exemplo-06 06-driver-proprio/main.cpp)

# Os dois que dependem do gerador só entram quando há Python.
if(Python3_Interpreter_FOUND)
    molecula_exemplo(exemplo-02 02-gerador/main.cpp)
    molecula_exemplo_gerado(exemplo-02 ${CMAKE_CURRENT_SOURCE_DIR}/02-gerador/schema.toml)

    molecula_exemplo(exemplo-03 03-consultas/main.cpp)
    molecula_exemplo_gerado(exemplo-03 ${CMAKE_CURRENT_SOURCE_DIR}/03-consultas/schema.toml)
else()
    message(WARNING "Sem Python3: os exemplos 02 e 03 (gerador) ficam de fora.")
endif()
