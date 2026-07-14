cmake_minimum_required(VERSION 3.21)
project(UsageBar VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core DBus Network Svg Widgets)

add_library(usagebar_core
    src/notificationtracker.cpp
    src/providers.cpp
    src/singleinstance.cpp
    src/usage.cpp
    src/usagebackend.cpp
)
target_include_directories(usagebar_core PUBLIC src)
target_link_libraries(usagebar_core PUBLIC Qt6::Core Qt6::Network)

add_executable(usagebar
    src/main.cpp
    src/mainwindow.cpp
    src/settingswindow.cpp
)
qt_add_resources(usagebar icons PREFIX "/" FILES
    assets/usagebar-256.png
    assets/usagebar-symbolic.svg
    assets/provider-codex.svg
    assets/provider-claude.svg
    assets/provider-cursor.svg
    assets/provider-factory.svg
    assets/provider-gemini.svg
    assets/provider-copilot.svg
)
target_compile_definitions(usagebar PRIVATE USAGEBAR_VERSION="${PROJECT_VERSION}")
target_link_libraries(usagebar PRIVATE usagebar_core Qt6::DBus Qt6::Svg Qt6::Widgets)

install(TARGETS usagebar RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES assets/usagebar-256.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    RENAME usagebar.png)
install(FILES assets/usagebar-symbolic.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/symbolic/apps)
install(FILES packaging/io.github.usagebar.UsageBar.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES LICENSE THIRD_PARTY_LICENSES.md README.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/usagebar)

if(DEFINED CODEXBAR_CLI_PATH)
    install(PROGRAMS "${CODEXBAR_CLI_PATH}" DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/usagebar RENAME CodexBarCLI)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
    add_executable(usagebar_tests tests/usage_test.cpp)
    target_link_libraries(usagebar_tests PRIVATE usagebar_core Qt6::Test)
    add_test(NAME usagebar_tests COMMAND usagebar_tests)
endif()
