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
    src/common/logging.cpp
)

target_include_directories(pastetry_common PUBLIC src)

target_link_libraries(pastetry_common
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Sql
        Qt6::Network
)

add_executable(pastetry-clipd
    src/clipd/main.cpp
    src/clipd/clipboard_daemon.cpp
)

target_link_libraries(pastetry-clipd PRIVATE pastetry_common)

add_executable(pastetry-clip-ui
    src/clip-ui/app_controller.cpp
    src/clip-ui/clipboard_inspector_dialog.cpp
    src/clip-ui/global_shortcut_service.cpp
    src/clip-ui/main.cpp
    src/clip-ui/history_model.cpp
    src/clip-ui/main_window.cpp
    src/clip-ui/preview_text_delegate.cpp
    src/clip-ui/quick_paste_dialog.cpp
    src/clip-ui/shortcut_config.cpp
    src/clip-ui/settings_dialog.cpp
)

target_link_libraries(pastetry-clip-ui
    PRIVATE
        pastetry_common
        Qt6::Widgets
)

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_include_directories(pastetry-clip-ui PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(pastetry-clip-ui PRIVATE ${X11_LIBRARIES})
    if(X11_XTest_FOUND)
        target_link_libraries(pastetry-clip-ui PRIVATE ${X11_XTest_LIB})
        target_compile_definitions(pastetry-clip-ui PRIVATE PASTETRY_HAVE_XTEST=1)
    endif()
endif()

enable_testing()
add_executable(repository_tests
    tests/repository_tests.cpp
)

target_link_libraries(repository_tests PRIVATE pastetry_common Qt6::Test)

add_test(NAME repository_tests COMMAND repository_tests)
