# Profils metier et automatismes. Sans GUI : les regles de reperage et les
# rapports se testent tete baissee, sans ouvrir de fenetre.
add_library(dessins_rules STATIC
    profile.h    profile.cpp
    numbering.h  numbering.cpp
    reports.h    reports.cpp
    ladder.h     ladder.cpp
)

target_link_libraries(dessins_rules PUBLIC dessins_core)
set_target_properties(dessins_rules PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(Dessins::Rules ALIAS dessins_rules)
