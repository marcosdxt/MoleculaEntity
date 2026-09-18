# The examples are built AND executed by ctest.
#
# An example that doesn't compile is worse than no example at all: someone new
# trusts it, loses an afternoon, and concludes the library is what's broken.
# Tying them to the suite is what prevents that — when the API changes, they break
# along with the tests, not months later in somebody's issue.

add_custom_target(examples)

# Generates entities and repositories from a schema, into the example's build dir.
function(molecula_example_generated target schema)
    set(destination ${CMAKE_CURRENT_BINARY_DIR}/${target}-generated)

    add_custom_command(
        OUTPUT ${destination}/Entities.hpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${destination}
        COMMAND Python3::Interpreter ${MOLECULA_GENERATOR} ${schema} -o ${destination}
        DEPENDS ${MOLECULA_GENERATOR} ${schema}
        COMMENT "Generating the schema for example ${target}"
        VERBATIM)

    add_custom_target(${target}-generate DEPENDS ${destination}/Entities.hpp)
    add_dependencies(${target} ${target}-generate)
    target_include_directories(${target} PRIVATE ${destination})
endfunction()

function(molecula_example target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE MoleculaEntity::SQLite3)
    add_dependencies(examples ${target})

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    # Example 04 writes a file into the working directory; each one runs in its own.
    add_test(NAME ${target} COMMAND ${target} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endfunction()

molecula_example(example-01 01-basics/main.cpp)
molecula_example(example-04 04-migrations/main.cpp)
molecula_example(example-05 05-errors-and-transactions/main.cpp)
molecula_example(example-06 06-custom-driver/main.cpp)

# The two that need the generator only join in when Python is available.
if(Python3_Interpreter_FOUND)
    molecula_example(example-02 02-generator/main.cpp)
    molecula_example_generated(example-02 ${CMAKE_CURRENT_SOURCE_DIR}/02-generator/schema.toml)

    molecula_example(example-03 03-queries/main.cpp)
    molecula_example_generated(example-03 ${CMAKE_CURRENT_SOURCE_DIR}/03-queries/schema.toml)
else()
    message(WARNING "No Python3: examples 02 and 03 (generator) are left out.")
endif()
