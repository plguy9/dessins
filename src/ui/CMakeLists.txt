add_library(dessins_ui STATIC
    document.h        document.cpp
    folioview.h       folioview.cpp
    symbolpalette.h   symbolpalette.cpp
    propertiespanel.h propertiespanel.cpp
    folionavigator.h  folionavigator.cpp
    reportpanel.h     reportpanel.cpp
    mainwindow.h      mainwindow.cpp
)

set_target_properties(dessins_ui PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_link_libraries(dessins_ui PUBLIC
    dessins_core dessins_symbols dessins_rules dessins_io dessins_render
    Qt6::Widgets Qt6::PrintSupport)
add_library(Dessins::Ui ALIAS dessins_ui)
