# Le coeur ne connait que Qt6::Core : ni widgets, ni GUI. C'est ce qui le rend
# testable sans ecran en integration continue et reutilisable tel quel par un
# futur outil en ligne de commande.
add_library(dessins_core STATIC
    geometry.h        geometry.cpp
    primitive.h       primitive.cpp
    jsonutils.h       jsonutils.cpp
    entity.h          entity.cpp
    entities.h        entities.cpp
    symboldef.h       symboldef.cpp
    symbollibrary.h   symbollibrary.cpp
    folio.h           folio.cpp
    project.h         project.cpp
    netlist.h         netlist.cpp
    command.h         command.cpp
    documentcommands.h documentcommands.cpp
)

target_link_libraries(dessins_core PUBLIC Qt6::Core)
target_include_directories(dessins_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(dessins_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(Dessins::Core ALIAS dessins_core)
