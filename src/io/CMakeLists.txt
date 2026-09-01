# zlib est fourni par le systeme sous Linux et macOS. Le runner Windows n'en
# a pas : on le compile alors depuis les sources officielles, en statique,
# plutot que d'exiger un gestionnaire de paquets tiers.
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    message(STATUS "zlib introuvable — compilation depuis les sources (FetchContent)")
    include(FetchContent)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(zlib
        URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
        URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23)
    FetchContent_MakeAvailable(zlib)
    target_include_directories(zlibstatic PUBLIC ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

add_library(arcus_io STATIC
    zip.h        zip.cpp
    dsnfile.h    dsnfile.cpp
    csvexport.h  csvexport.cpp
    dxfexport.h  dxfexport.cpp
)

target_link_libraries(arcus_io PUBLIC arcus_core arcus_rules PRIVATE ZLIB::ZLIB)
set_target_properties(arcus_io PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(Arcus::Io ALIAS arcus_io)
