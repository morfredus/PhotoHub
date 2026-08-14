cmake_minimum_required(VERSION 3.21)

# =============================================================================
#  PhotoHub - application desktop de gestion de la phototheque.
#
#  Client PUR de morfPhoto : il ne lit jamais les fichiers photo, ne lance jamais
#  ExifTool, ne connait pas SQLite. Il decouvre morfPhoto sur le LAN par sa
#  CAPACITE morfBeacon ("photo_index") et dialogue avec son API HTTP /api/v1.
#  Qt Widgets + Network uniquement. Multi-plateforme (Windows, Linux x64, ARM64).
# =============================================================================

# Version pilotee par le fichier VERSION (convention morfSystem).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PHOTOHUB_VERSION_FROM_FILE LIMIT_COUNT 1)
string(STRIP "${PHOTOHUB_VERSION_FROM_FILE}" PHOTOHUB_VERSION_FROM_FILE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

project(PhotoHub VERSION "${PHOTOHUB_VERSION_FROM_FILE}" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)                 # compile les ressources Qt (.qrc)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# morfUpdate : verification des mises a jour (GitHub Releases). Bibliotheque
# vendoree dans third_party/morf/update, liee statiquement (source de verite :
# depot morfUpdate ; resync via scripts/sync-morf). Coeur + couche Widgets.
add_subdirectory(third_party/morf/update)

# Ressources : icone Qt (fenetre) + .rc Windows (icone de l'exe).
set(APP_RESOURCES resources/app.qrc)
if(WIN32)
    list(APPEND APP_RESOURCES resources/app.rc)
endif()

# WIN32 : application graphique (pas de console noire au lancement sous Windows).
add_executable(PhotoHub WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/BeaconDiscovery.cpp
    src/MorfPhotoClient.cpp
    src/MainWindow.h
    src/BeaconDiscovery.h
    src/MorfPhotoClient.h
    ${APP_RESOURCES}
)

target_link_libraries(PhotoHub PRIVATE
    Qt6::Widgets Qt6::Network
    morfUpdate::morfUpdate morfUpdate::Widgets)
target_compile_definitions(PhotoHub PRIVATE PHOTOHUB_VERSION="${PROJECT_VERSION}")

if(WIN32)
    target_link_libraries(PhotoHub PRIVATE ws2_32)   # sockets UDP (decouverte LAN)
endif()
if(MSVC)
    target_compile_options(PhotoHub PRIVATE /utf-8)
endif()

# --- Deploiement Windows : rendre le dossier de build autonome ------------
# Un exe Qt a besoin de ses DLL (Qt6 + plugin platforms/qwindows.dll + runtime)
# a cote du binaire. Sans cela, il ne se lance qu'avec MinGW sur le PATH.
if(WIN32)
    # 1) windeployqt : DLL Qt, plugins et runtime du compilateur.
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET PhotoHub POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}" --no-translations --compiler-runtime "$<TARGET_FILE:PhotoHub>"
            COMMENT "Deploiement des DLL Qt a cote de l'executable")
    endif()
    # 2) MinGW : windeployqt ne copie QUE les DLL Qt. Ce script complete avec les
    #    DLL non-Qt de /mingw64/bin (runtime gcc, ICU, pcre2, zlib...) pour que
    #    l'exe soit lancable en dehors du shell MSYS2.
    if(NOT MSVC)
        add_custom_command(TARGET PhotoHub POST_BUILD
            COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/windows/deploy-mingw.sh" "$<TARGET_FILE_DIR:PhotoHub>"
            COMMENT "Copie des DLL MinGW a cote de l'executable")
    endif()
endif()
