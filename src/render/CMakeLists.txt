# Le rendu depend de QtGui, pas de QtWidgets : la meme peinture sert a la vue,
# a l'apercu, au PDF et au rendu bitmap sans ecran.
add_library(arcus_render STATIC
    renderstyle.h  renderstyle.cpp
    foliopainter.h foliopainter.cpp
    pdfexport.h    pdfexport.cpp
)

target_link_libraries(arcus_render PUBLIC arcus_core Qt6::Gui)
set_target_properties(arcus_render PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(Arcus::Render ALIAS arcus_render)
