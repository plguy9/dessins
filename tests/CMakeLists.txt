find_package(Catch2 3 QUIET)

if(NOT Catch2_FOUND)
    message(WARNING "Catch2 3 introuvable — la suite de tests est desactivee.")
    return()
endif()

add_executable(arcus_tests
    main.cpp
    test_geometry.cpp
    test_entities.cpp
    test_document.cpp
    test_netlist.cpp
    test_commands.cpp
    test_io.cpp
    test_rules.cpp
    test_plc.cpp
    test_audit.cpp
    test_circuitcopy.cpp
    test_edittools.cpp
    test_render.cpp
    test_snap.cpp
    test_wiretools.cpp
    test_ladder.cpp
    test_wiretype.cpp
    test_crossref.cpp
    test_reportplacer.cpp
    test_coordinateentry.cpp
)

target_link_libraries(arcus_tests PRIVATE
    arcus_core arcus_symbols arcus_rules arcus_io arcus_render
    Catch2::Catch2)

include(Catch)
catch_discover_tests(arcus_tests)

# --------------------------------------------------------------------------
# Tests d'interface. Ils construisent de vraies fenetres, donc un binaire
# separe avec QApplication plutot que QGuiApplication.
if(TARGET arcus_ui)
    add_executable(arcus_ui_tests
        uimain.cpp
        test_ui.cpp
    )
    set_target_properties(arcus_ui_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(arcus_ui_tests PRIVATE arcus_ui Catch2::Catch2)
    catch_discover_tests(arcus_ui_tests)
endif()
