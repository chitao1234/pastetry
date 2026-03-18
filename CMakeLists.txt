cmake_minimum_required(VERSION 3.21)
project(pastetry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Sql Network Test)

add_library(pastetry_common
    src/common/app_paths.cpp
    src/common/blob_store.cpp
    src/common/clipboard_repository.cpp
    src/common/ipc_client.cpp
    src/common/ipc_protocol.cpp
)

target_include_directories(pastetry_common PUBLIC src)

target_link_libraries(pastetry_common
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Sql
        Qt6::Network
)

add_executable(clipd
    src/clipd/main.cpp
    src/clipd/clipboard_daemon.cpp
)

target_link_libraries(clipd PRIVATE pastetry_common)

add_executable(clip-ui
    src/clip-ui/main.cpp
    src/clip-ui/history_model.cpp
    src/clip-ui/main_window.cpp
)

target_link_libraries(clip-ui
    PRIVATE
        pastetry_common
        Qt6::Widgets
)

enable_testing()
add_executable(repository_tests
    tests/repository_tests.cpp
)

target_link_libraries(repository_tests PRIVATE pastetry_common Qt6::Test)

add_test(NAME repository_tests COMMAND repository_tests)
