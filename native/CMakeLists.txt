cmake_minimum_required(VERSION 3.21)
project(TakoyoNative LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(TakoyoNative WIN32
    src/main.cpp
    src/theme.cpp
    src/icons.cpp
    src/util.cpp
    src/config.cpp
    src/store.cpp
    src/backend.cpp
    src/widgets.cpp
    src/composer.cpp
    src/homeview.cpp
    src/chatview.cpp
    src/sidebar.cpp
    src/artpanel.cpp
    src/dialogs.cpp
    src/mainwindow.cpp
)

target_include_directories(TakoyoNative PRIVATE src)
target_link_libraries(TakoyoNative PRIVATE Qt6::Widgets Qt6::Network)

if(MSVC)
    target_compile_options(TakoyoNative PRIVATE /W4)
else()
    target_compile_options(TakoyoNative PRIVATE -Wall -Wextra)
endif()
