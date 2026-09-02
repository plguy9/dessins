cmake_minimum_required(VERSION 3.21)

project(Arcus
    VERSION 0.9.1
    DESCRIPTION "Logiciel de dessin electrique"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(ARCUS_BUILD_TESTS "Construire la suite de tests" ON)
option(ARCUS_BUILD_GUI   "Construire l'application graphique" ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui)
if(ARCUS_BUILD_GUI)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets PrintSupport)
endif()

qt_standard_project_setup()

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src/core)
add_subdirectory(src/symbols)
add_subdirectory(src/rules)
add_subdirectory(src/render)
add_subdirectory(src/io)
if(ARCUS_BUILD_GUI)
    add_subdirectory(src/ui)
    add_subdirectory(src/app)
endif()

add_subdirectory(tools/sample)

if(ARCUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "Arcus ${PROJECT_VERSION} — Qt ${Qt6_VERSION} — ${CMAKE_BUILD_TYPE}")
