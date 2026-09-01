qt_add_executable(arcus main.cpp)
# Une seule source de verite pour le numero de version : project() dans le
# CMakeLists racine. Le tag git doit lui correspondre (v0.1.0 pour 0.1.0).
target_compile_definitions(arcus PRIVATE ARCUS_VERSION="${PROJECT_VERSION}")
if(WIN32)
    # L'icone de l'executable. L'icone de la fenetre, elle, est dessinee a
    # l'execution (Icons::appIcon) et n'a besoin d'aucune ressource.
    target_sources(arcus PRIVATE arcus.rc)
endif()
target_link_libraries(arcus PRIVATE arcus_ui)
set_target_properties(arcus PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    OUTPUT_NAME arcus)

install(TARGETS arcus RUNTIME DESTINATION bin BUNDLE DESTINATION .)
