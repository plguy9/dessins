# Profils metier et automatismes. Sans GUI : les regles de reperage et les
# rapports se testent tete baissee, sans ouvrir de fenetre.
add_library(dessins_rules STATIC
    profile.h    profile.cpp
    numbering.h  numbering.cpp
    reports.h    reports.cpp
    ladder.h     ladder.cpp
    crossref.h   crossref.cpp
    reportplacer.h reportplacer.cpp
    catalog.h    catalog.cpp
)

# Le catalogue fabricant est embarque comme la bibliotheque de symboles : le
# logiciel doit proposer des references des le premier lancement.
qt_add_resources(dessins_rules "builtin_catalog"
    PREFIX "/catalog"
    BASE ${CMAKE_SOURCE_DIR}/catalog
    FILES ${CMAKE_SOURCE_DIR}/catalog/catalogue.json
)

target_link_libraries(dessins_rules PUBLIC dessins_core)
set_target_properties(dessins_rules PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
add_library(Dessins::Rules ALIAS dessins_rules)
