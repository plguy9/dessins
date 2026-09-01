# Le rendu depend de QtGui, pas de QtWidgets : la meme peinture sert a la vue,
# a l'apercu, au PDF et au rendu bitmap sans ecran.
add_library(dessins_render STATIC
    renderstyle.h  renderstyle.cpp
    foliopainter.h foliopainter.cpp
    pdfexport.h    pdfexport.cpp
)

target_link_libraries(dessins_render PUBLIC dessins_core Qt6::Gui)
set_target_properties(dessins_render PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(Dessins::Render ALIAS dessins_render)
