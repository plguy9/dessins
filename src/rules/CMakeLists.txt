# Profils metier et automatismes. Sans GUI : les regles de reperage et les
# rapports se testent tete baissee, sans ouvrir de fenetre.
add_library(arcus_rules STATIC
    profile.h    profile.cpp
    numbering.h  numbering.cpp
    reports.h    reports.cpp
    ladder.h     ladder.cpp
    crossref.h   crossref.cpp
    reportplacer.h reportplacer.cpp
    catalog.h    catalog.cpp
    plc.h        plc.cpp
    audit.h      audit.cpp
)

# Le catalogue fabricant est embarque comme la bibliotheque de symboles : le
# logiciel doit proposer des references des le premier lancement.
qt_add_resources(arcus_rules "builtin_catalog"
    PREFIX "/catalog"
    BASE ${CMAKE_SOURCE_DIR}/catalog
    FILES ${CMAKE_SOURCE_DIR}/catalog/catalogue.json
)

# La base des modules d'automates suit le meme principe : le logiciel doit
# savoir poser une carte d'entrees-sorties des le premier lancement.
qt_add_resources(arcus_rules "builtin_plc"
    PREFIX "/plc"
    BASE ${CMAKE_SOURCE_DIR}/plc
    FILES ${CMAKE_SOURCE_DIR}/plc/automates.json
)

target_link_libraries(arcus_rules PUBLIC arcus_core)
set_target_properties(arcus_rules PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
add_library(Arcus::Rules ALIAS arcus_rules)
