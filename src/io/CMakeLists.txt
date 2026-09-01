find_package(ZLIB REQUIRED)

add_library(dessins_io STATIC
    zip.h        zip.cpp
    dsnfile.h    dsnfile.cpp
    csvexport.h  csvexport.cpp
    dxfexport.h  dxfexport.cpp
)

target_link_libraries(dessins_io PUBLIC dessins_core dessins_rules PRIVATE ZLIB::ZLIB)
set_target_properties(dessins_io PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(Dessins::Io ALIAS dessins_io)
