# Outil de generation du projet d'exemple. Il exerce la bibliotheque, la
# connectivite, le reperage, les rapports, le rendu et les trois formats de
# sortie en une seule passe : c'est aussi une verification de bout en bout.
add_executable(arcus_sample main.cpp)
target_link_libraries(arcus_sample PRIVATE
    arcus_core arcus_symbols arcus_rules arcus_io arcus_render)
