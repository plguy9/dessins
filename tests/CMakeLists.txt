find_package(Catch2 3 QUIET)

if(NOT Catch2_FOUND)
    message(WARNING "Catch2 3 introuvable — la suite de tests est desactivee.")
    return()
endif()

add_executable(dessins_tests
    test_geometry.cpp
    test_entities.cpp
    test_document.cpp
    test_netlist.cpp
    test_commands.cpp
    test_io.cpp
    test_rules.cpp
)

target_link_libraries(dessins_tests PRIVATE
    dessins_core dessins_symbols dessins_rules dessins_io
    Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(dessins_tests)
