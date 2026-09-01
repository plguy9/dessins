qt_add_executable(dessins main.cpp)
target_link_libraries(dessins PRIVATE dessins_ui)
set_target_properties(dessins PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    OUTPUT_NAME dessins)

install(TARGETS dessins RUNTIME DESTINATION bin BUNDLE DESTINATION .)
