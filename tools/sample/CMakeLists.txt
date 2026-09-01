# Outil de generation du projet d'exemple. Il exerce la bibliotheque, la
# connectivite, le reperage, les rapports, le rendu et les trois formats de
# sortie en une seule passe : c'est aussi une verification de bout en bout.
add_executable(dessins_sample main.cpp)
target_link_libraries(dessins_sample PRIVATE
    dessins_core dessins_symbols dessins_rules dessins_io dessins_render)
