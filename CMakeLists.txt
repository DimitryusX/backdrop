cmake_minimum_required(VERSION 3.20)
project(backdrop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(ADWAITA REQUIRED IMPORTED_TARGET libadwaita-1)
find_package(Intl)
find_program(MSGFMT_EXECUTABLE msgfmt)

include(GNUInstallDirs)

find_package(nlohmann_json 3.10 QUIET)
set(BACKDROP_JSON_TARGET "")
if(nlohmann_json_FOUND)
  set(BACKDROP_JSON_TARGET nlohmann_json::nlohmann_json)
else()
  find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)
  if(NLOHMANN_JSON_INCLUDE_DIR)
    add_library(backdrop_json INTERFACE)
    target_include_directories(backdrop_json INTERFACE "${NLOHMANN_JSON_INCLUDE_DIR}")
    set(BACKDROP_JSON_TARGET backdrop_json)
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann/json.hpp")
    add_library(backdrop_json INTERFACE)
    target_include_directories(backdrop_json INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
    set(BACKDROP_JSON_TARGET backdrop_json)
  else()
    message(FATAL_ERROR "nlohmann/json.hpp not found. Install nlohmann-json-devel or vendor third_party/nlohmann/json.hpp")
  endif()
endif()

add_executable(backdrop
  src/main.cpp
  src/i18n.cpp
  src/config.cpp
  src/storage.cpp
  src/wallpaper.cpp
  src/rotator.cpp
  src/daemon.cpp
  src/service.cpp
  src/app.cpp
)

set(GETTEXT_PACKAGE "backdrop")
set(BACKDROP_BUILD_LOCALEDIR "${CMAKE_CURRENT_BINARY_DIR}/locale")

target_include_directories(backdrop PRIVATE src)
target_compile_definitions(backdrop PRIVATE
  GETTEXT_PACKAGE="${GETTEXT_PACKAGE}"
  BACKDROP_LOCALEDIR="${CMAKE_INSTALL_FULL_LOCALEDIR}"
  BACKDROP_BUILD_LOCALEDIR="${BACKDROP_BUILD_LOCALEDIR}"
)
target_link_libraries(backdrop PRIVATE
  PkgConfig::GTK4
  PkgConfig::ADWAITA
  ${BACKDROP_JSON_TARGET}
)
if(Intl_FOUND)
  target_link_libraries(backdrop PRIVATE Intl::Intl)
endif()

# Translations (English = source strings in code; Ukrainian catalog below).
if(MSGFMT_EXECUTABLE)
  set(BACKDROP_MO_FILES "")
  foreach(lang uk)
    set(_po "${CMAKE_CURRENT_SOURCE_DIR}/po/${lang}.po")
    set(_mo_dir "${BACKDROP_BUILD_LOCALEDIR}/${lang}/LC_MESSAGES")
    set(_mo "${_mo_dir}/${GETTEXT_PACKAGE}.mo")
    add_custom_command(
      OUTPUT "${_mo}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_mo_dir}"
      COMMAND "${MSGFMT_EXECUTABLE}" -o "${_mo}" "${_po}"
      DEPENDS "${_po}"
      COMMENT "Compiling ${lang} translation"
      VERBATIM
    )
    list(APPEND BACKDROP_MO_FILES "${_mo}")
    install(FILES "${_mo}"
            DESTINATION "${CMAKE_INSTALL_LOCALEDIR}/${lang}/LC_MESSAGES")
  endforeach()
  add_custom_target(translations ALL DEPENDS ${BACKDROP_MO_FILES})
  add_dependencies(backdrop translations)
else()
  message(WARNING "msgfmt not found — Ukrainian translations will not be built")
endif()

install(TARGETS backdrop RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/io.nexol.Backdrop.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/metainfo/io.nexol.Backdrop.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES data/icons/hicolor/scalable/apps/io.nexol.Backdrop.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

# systemd --user unit (enable/start from the GUI via systemctl --user)
set(BACKDROP_EXECUTABLE "${CMAKE_INSTALL_FULL_BINDIR}/backdrop")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/data/systemd/user/backdrop.service.in"
  "${CMAKE_CURRENT_BINARY_DIR}/backdrop.service"
  @ONLY
)
set(BACKDROP_SYSTEMD_USER_DIR "lib/systemd/user")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/backdrop.service"
        DESTINATION "${BACKDROP_SYSTEMD_USER_DIR}")

# Local install helper: refresh desktop / icon caches when tools are available.
install(CODE "include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/PostInstall.cmake\")")

# `cmake --build <builddir> --target uninstall`
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  @ONLY
)
add_custom_target(uninstall
  COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  COMMENT "Uninstalling Backdrop files listed in install_manifest.txt"
)

# Source / binary archives via CPack (optional; RPM/Flatpak scripts preferred).
set(CPACK_PACKAGE_NAME "backdrop")
set(CPACK_PACKAGE_VENDOR "Nexol")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Minimal Linux wallpaper rotator")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://nexol.io")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_CONTACT "Dmytro <dmytro@nexol.io>")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
endif()
set(CPACK_GENERATOR "TGZ")
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_IGNORE_FILES
  "/build/"
  "/dist/"
  "/\\.git/"
  "/\\.vscode/"
  "/\\.flatpak-builder/"
  ".*~$"
)
include(CPack)
