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


# --- Enregistrement des compilations (best-effort) ------------------------
# Chronometre le vrai travail des compilateurs et signale la duree a
# morfAnalytics (POST /api/monitor/activity), QUEL QUE SOIT le declencheur :
# `cmake --build` direct, `morf upgrade` ou un deploiement morfDeploy (un
# marqueur evite le double comptage cote morfDeploy). Sans URL configuree
# (machine hors parc), c'est un no-op silencieux. A appeler AVANT les cibles.
include("${CMAKE_CURRENT_SOURCE_DIR}/third_party/morf/morfdeploy/record_compile.cmake")
morf_enable_compile_recording()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)                 # compile les ressources Qt (.qrc)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# morfUpdate : verification des mises a jour (GitHub Releases). Bibliotheque
# vendoree dans third_party/morf/update, liee statiquement (source de verite :
# depot morfUpdate ; resync via scripts/sync-morf). Coeur + couche Widgets.
add_subdirectory(third_party/morf/update)

# morfBeacon : annonce de presence sur le LAN (heartbeat UDP + /status), comme
# ComponentHub et SiteWatch. Bibliotheque vendoree dans third_party/morf/beacon
# (source de verite : depot morfBeacon ; resync via scripts/sync-morf).
add_subdirectory(third_party/morf/beacon)

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
    morfUpdate::morfUpdate morfUpdate::Widgets
    morfBeacon::morfBeacon)
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
    find_program(WINDEPLOYQT_EXE windeployqt
        HINTS "${Qt6_DIR}/../../../bin" "$ENV{QT_ROOT}/bin")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET PhotoHub POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}" --no-translations --compiler-runtime "$<TARGET_FILE:PhotoHub>"
            COMMENT "Deploiement des DLL Qt a cote de l'executable")
    endif()
    # 2) MinGW : windeployqt ne copie QUE les DLL Qt. Ce script complete avec les
    #    DLL non-Qt de /mingw64/bin (runtime gcc, ICU, pcre2, zlib...) pour que
    #    l'exe soit lancable en dehors du shell MSYS2.
    if(NOT MSVC)
        # NE PAS appeler `bash` du PATH : sous PowerShell, Windows le resout souvent
        # vers le Bash WSL, qui ne sait pas ouvrir un chemin « C:/... » de ce
        # workspace (« No such file or directory »). Sous VSCode le PATH a le Bash
        # MSYS2 en tete, d'ou l'erreur qui n'apparait que hors de l'IDE. On deduit
        # donc le Bash MSYS2 du compilateur MinGW actif, et on convertit les chemins.
        function(photohub_msys_path windows_path output)
            string(REGEX REPLACE "^([A-Za-z]):/" "/\\1/" converted "${windows_path}")
            string(SUBSTRING "${converted}" 1 1 drive)
            string(TOLOWER "${drive}" drive)
            string(SUBSTRING "${converted}" 2 -1 tail)
            set(${output} "/${drive}${tail}" PARENT_SCOPE)
        endfunction()
        photohub_msys_path("${CMAKE_SOURCE_DIR}/scripts/windows/deploy-mingw.sh" PHOTOHUB_DEPLOY_SCRIPT)
        photohub_msys_path("${CMAKE_CURRENT_BINARY_DIR}" PHOTOHUB_TARGET_DIR)
        # Le preset ne donne que g++.exe : resoudre le binaire reel, puis en
        # deduire mingw*/bin (ldd) et <msys>/usr/bin (bash, grep, awk). Aucun
        # C:/msys64 ni emplacement Qt en dur : une autre machine avec MSYS2
        # ailleurs, ou ucrt64 au lieu de mingw64, reste valide.
        set(_photohub_cxx "${CMAKE_CXX_COMPILER}")
        if(NOT EXISTS "${_photohub_cxx}")
            find_program(_photohub_cxx NAMES "${CMAKE_CXX_COMPILER}" g++.exe)
        endif()
        get_filename_component(PHOTOHUB_COMPILER_BIN "${_photohub_cxx}" DIRECTORY)
        get_filename_component(PHOTOHUB_MINGW_ROOT "${PHOTOHUB_COMPILER_BIN}" DIRECTORY)
        get_filename_component(PHOTOHUB_MSYS_ROOT "${PHOTOHUB_MINGW_ROOT}" DIRECTORY)
        find_program(PHOTOHUB_MSYS_BASH bash.exe
            HINTS "${PHOTOHUB_MSYS_ROOT}/usr/bin"
            NO_DEFAULT_PATH)
        if(PHOTOHUB_MSYS_BASH)
            # Depuis PowerShell, bash.exe demarre sans /usr/bin sur PATH.
            # cmake -E env + VERBATIM : PATH = usr/bin de CETTE install MSYS2
            # + dossier du g++ (ldd), jamais C:/msys64 en dur.
            add_custom_command(TARGET PhotoHub POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E env
                    "PATH=${PHOTOHUB_MSYS_ROOT}/usr/bin;${PHOTOHUB_COMPILER_BIN}"
                    --
                    "${PHOTOHUB_MSYS_BASH}" "${PHOTOHUB_DEPLOY_SCRIPT}" "${PHOTOHUB_TARGET_DIR}"
                VERBATIM
                COMMENT "Copie des DLL MinGW a cote de l'executable")
        else()
            message(WARNING "MSYS2 Bash introuvable ; les DLL MinGW non-Qt ne sont pas copiees")
        endif()
    endif()
endif()

# Signale la duree de compile a morfAnalytics apres le lien de l'executable.
morf_record_target_build(PhotoHub "PhotoHub")
