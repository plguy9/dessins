add_library(arcus_ui STATIC
    theme.h           theme.cpp
    appearance.h      appearance.cpp
    docktitle.h       docktitle.cpp
    dockrail.h        dockrail.cpp
    document.h        document.cpp
    folioview.h       folioview.cpp
    symbolpalette.h   symbolpalette.cpp
    propertiespanel.h propertiespanel.cpp
    propertiesdialog.h propertiesdialog.cpp
    folionavigator.h  folionavigator.cpp
    reportpanel.h     reportpanel.cpp
    symboleditor.h    symboleditor.cpp
    draftingsettingsdialog.h draftingsettingsdialog.cpp
    pagesetupdialog.h pagesetupdialog.cpp
    commandline.h     commandline.cpp
    commandpalette.h  commandpalette.cpp
    startpage.h       startpage.cpp
    ladderdialog.h    ladderdialog.cpp
    wiretypedialog.h  wiretypedialog.cpp
    componentdialog.h componentdialog.cpp
    surferdialog.h    surferdialog.cpp
    symbolswapdialog.h symbolswapdialog.cpp
    terminalstripdialog.h terminalstripdialog.cpp
    plcdialog.h       plcdialog.cpp
    auditdialog.h     auditdialog.cpp
    findreplacedialog.h findreplacedialog.cpp
    arraydialog.h     arraydialog.cpp
    busdialog.h       busdialog.cpp
    ribbon.h          ribbon.cpp
    mainwindow.h      mainwindow.cpp
)

set_target_properties(arcus_ui PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_link_libraries(arcus_ui PUBLIC
    arcus_core arcus_symbols arcus_rules arcus_io arcus_render
    Qt6::Widgets Qt6::PrintSupport)
add_library(Arcus::Ui ALIAS arcus_ui)
