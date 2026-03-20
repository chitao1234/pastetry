cmake_minimum_required(VERSION 3.21)
project(pastetry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Sql Network Test)
find_package(Qt6 6.4 QUIET COMPONENTS DBus WaylandClient)

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

add_library(pastetry_ui
    src/clip-ui/app_controller.cpp
    src/clip-ui/clipboard_inspector_dialog.cpp
    src/clip-ui/global_shortcut_service.cpp
    src/clip-ui/ipc_async_runner.cpp
    src/clip-ui/history_model.cpp
    src/clip-ui/main_window.cpp
    src/clip-ui/preview_text_delegate.cpp
    src/clip-ui/quick_paste_dialog.cpp
    src/clip-ui/shortcut_config.cpp
    src/clip-ui/settings_dialog.cpp
)

target_link_libraries(pastetry_ui
    PUBLIC
        pastetry_common
        Qt6::Widgets
)

if(Qt6DBus_FOUND)
    target_link_libraries(pastetry_ui PRIVATE Qt6::DBus)
    target_compile_definitions(pastetry_ui PRIVATE PASTETRY_HAVE_DBUS=1)
endif()

if(Qt6WaylandClient_FOUND)
    target_link_libraries(pastetry_ui PRIVATE Qt6::WaylandClient)
    target_compile_definitions(pastetry_ui PRIVATE PASTETRY_HAVE_QT_WAYLAND_CLIENT=1)
endif()

if(UNIX AND NOT APPLE)
    find_package(X11 QUIET)
    if(X11_FOUND)
        target_include_directories(pastetry_ui PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(pastetry_ui PRIVATE ${X11_LIBRARIES})
        target_compile_definitions(pastetry_ui PRIVATE PASTETRY_HAVE_X11=1)
        if(X11_XTest_FOUND)
            target_link_libraries(pastetry_ui PRIVATE ${X11_XTest_LIB})
            target_compile_definitions(pastetry_ui PRIVATE PASTETRY_HAVE_XTEST=1)
        endif()
    endif()
endif()

add_executable(pastetry-clip-ui
    src/clip-ui/main.cpp
)

target_link_libraries(pastetry-clip-ui PRIVATE pastetry_ui)

enable_testing()
add_executable(repository_tests
    tests/repository_tests.cpp
)

target_link_libraries(repository_tests PRIVATE pastetry_common Qt6::Test)

add_test(NAME repository_tests COMMAND repository_tests)

add_executable(app_controller_integration_tests
    tests/app_controller_integration_tests.cpp
)
target_link_libraries(app_controller_integration_tests
    PRIVATE
        pastetry_ui
        Qt6::Test
)
add_test(NAME app_controller_integration_tests COMMAND app_controller_integration_tests)
set_tests_properties(app_controller_integration_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(quick_paste_dialog_tests
    tests/quick_paste_dialog_tests.cpp
)
target_link_libraries(quick_paste_dialog_tests
    PRIVATE
        pastetry_ui
        Qt6::Test
)
add_test(NAME quick_paste_dialog_tests COMMAND quick_paste_dialog_tests)
set_tests_properties(quick_paste_dialog_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
