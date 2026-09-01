qt_add_executable(dessins main.cpp)
if(WIN32)
    # L'icone de l'executable. L'icone de la fenetre, elle, est dessinee a
    # l'execution (Icons::appIcon) et n'a besoin d'aucune ressource.
    target_sources(dessins PRIVATE dessins.rc)
endif()
target_link_libraries(dessins PRIVATE dessins_ui)
set_target_properties(dessins PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    OUTPUT_NAME dessins)

install(TARGETS dessins RUNTIME DESTINATION bin BUNDLE DESTINATION .)
