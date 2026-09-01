add_library(dessins_ui STATIC
    theme.h           theme.cpp
    document.h        document.cpp
    folioview.h       folioview.cpp
    symbolpalette.h   symbolpalette.cpp
    propertiespanel.h propertiespanel.cpp
    folionavigator.h  folionavigator.cpp
    reportpanel.h     reportpanel.cpp
    symboleditor.h    symboleditor.cpp
    draftingsettingsdialog.h draftingsettingsdialog.cpp
    pagesetupdialog.h pagesetupdialog.cpp
    commandline.h     commandline.cpp
    ladderdialog.h    ladderdialog.cpp
    wiretypedialog.h  wiretypedialog.cpp
    componentdialog.h componentdialog.cpp
    surferdialog.h    surferdialog.cpp
    mainwindow.h      mainwindow.cpp
)

set_target_properties(dessins_ui PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_link_libraries(dessins_ui PUBLIC
    dessins_core dessins_symbols dessins_rules dessins_io dessins_render
    Qt6::Widgets Qt6::PrintSupport)
add_library(Dessins::Ui ALIAS dessins_ui)
