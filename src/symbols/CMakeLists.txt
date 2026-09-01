# La bibliotheque integree est embarquee dans le binaire : le logiciel doit
# demarrer avec ses symboles sur un poste ou rien n'est installe a cote.
# CONFIGURE_DEPENDS relance la configuration quand un symbole est ajoute.
file(GLOB_RECURSE DESSINS_BUILTIN_SYMBOLS CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/libraries/*.json)

add_library(dessins_symbols STATIC
    librarystore.h librarystore.cpp
)

qt_add_resources(dessins_symbols "builtin_libraries"
    PREFIX "/libraries"
    BASE ${CMAKE_SOURCE_DIR}/libraries
    FILES ${DESSINS_BUILTIN_SYMBOLS}
)

target_link_libraries(dessins_symbols PUBLIC dessins_core)
set_target_properties(dessins_symbols PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(Dessins::Symbols ALIAS dessins_symbols)
