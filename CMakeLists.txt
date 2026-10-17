cmake_minimum_required(VERSION 3.21)
project(EasyLunii VERSION 0.1.0 LANGUAGES CXX)

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
    src/core/platform.cpp
    src/core/storydevice.cpp
    src/core/device.cpp
    src/core/flamdevice.cpp
    src/core/liff.cpp
    src/core/packinfo.cpp
    res/core.qrc
)
target_include_directories(luniicore PUBLIC src)
target_link_libraries(luniicore PUBLIC Qt6::Core Qt6::Gui Qt6::Network OpenSSL::Crypto PkgConfig::LIBARCHIVE)
target_compile_options(luniicore PRIVATE -Wall -Wextra)

# --- Outil en ligne de commande (tests et scripts)
add_executable(easylunii-cli src/cli/main.cpp)
target_link_libraries(easylunii-cli PRIVATE luniicore)

# --- Application graphique (bibliothèque partagée avec les tests d'interface)
add_library(luniiapp STATIC
    src/app/mainwindow.cpp
    src/app/devicewatcher.cpp
    src/app/jobqueue.cpp
    src/app/library.cpp
    src/app/librarymodel.cpp
    src/app/librarypage.cpp
    src/app/transferpanel.cpp
    src/app/devicepanel.cpp
    src/app/editorpage.cpp
    src/app/editor/graphitems.cpp
    src/app/editor/audiotransport.cpp
    src/app/editor/simulator.cpp
    src/app/editor/wizard.cpp
)
target_link_libraries(luniiapp PUBLIC luniicore Qt6::Widgets Qt6::Concurrent Qt6::Multimedia Qt6::Sql)
target_compile_options(luniiapp PRIVATE -Wall -Wextra)

if(WIN32)
    # icône de l'exécutable et application sans console
    add_executable(easylunii WIN32 src/app/main.cpp res/app.qrc res/easylunii.rc)
else()
    add_executable(easylunii src/app/main.cpp res/app.qrc)
endif()
target_compile_definitions(easylunii PRIVATE EASYLUNII_VERSION="${PROJECT_VERSION}")
target_link_libraries(easylunii PRIVATE luniiapp)

option(EASYLUNII_TESTS "Tests d'interface" ON)
if(EASYLUNII_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(easylunii-uitest tests/uitest.cpp)
    target_link_libraries(easylunii-uitest PRIVATE luniiapp Qt6::Test)
endif()

install(TARGETS easylunii easylunii-cli RUNTIME DESTINATION bin)
if(NOT WIN32)
    install(FILES res/easylunii.desktop DESTINATION share/applications)
    foreach(size 16 24 32 48 64 128 256 512)
        install(FILES res/icons/easylunii-${size}.png
                DESTINATION share/icons/hicolor/${size}x${size}/apps RENAME easylunii.png)
    endforeach()
endif()
