cmake_minimum_required(VERSION 3.21)
project(LuniiNatif VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent Multimedia Sql)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBARCHIVE REQUIRED IMPORTED_TARGET libarchive)

# --- Cœur : accès aux Lunii, formats, conversions (sans interface)
add_library(luniicore STATIC
    src/core/crypto.cpp
    src/core/archive.cpp
    src/core/storydb.cpp
    src/core/studiopack.cpp
    src/core/convert.cpp
    src/core/packtype.cpp
    src/core/device.cpp
    src/core/packinfo.cpp
    res/core.qrc
)
target_include_directories(luniicore PUBLIC src)
target_link_libraries(luniicore PUBLIC Qt6::Core Qt6::Gui Qt6::Network OpenSSL::Crypto PkgConfig::LIBARCHIVE)
target_compile_options(luniicore PRIVATE -Wall -Wextra)

# --- Outil en ligne de commande (tests et scripts)
add_executable(lunii-natif-cli src/cli/main.cpp)
target_link_libraries(lunii-natif-cli PRIVATE luniicore)

# --- Application graphique
add_executable(lunii-natif
    src/app/main.cpp
    src/app/mainwindow.cpp
    src/app/devicewatcher.cpp
    src/app/jobqueue.cpp
    src/app/library.cpp
    src/app/librarymodel.cpp
    src/app/librarypage.cpp
    src/app/devicepanel.cpp
    src/app/editorpage.cpp
    src/app/editor/graphitems.cpp
    src/app/editor/simulator.cpp
    src/app/editor/wizard.cpp
    res/app.qrc
)
target_compile_definitions(lunii-natif PRIVATE LUNII_NATIF_VERSION="${PROJECT_VERSION}")
target_link_libraries(lunii-natif PRIVATE luniicore Qt6::Widgets Qt6::Concurrent Qt6::Multimedia Qt6::Sql)
target_compile_options(lunii-natif PRIVATE -Wall -Wextra)

install(TARGETS lunii-natif lunii-natif-cli RUNTIME DESTINATION bin)
install(FILES res/lunii-natif.desktop DESTINATION share/applications)
install(FILES res/lunii-natif.png DESTINATION share/icons/hicolor/512x512/apps)
