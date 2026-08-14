# Refresh desktop/icon caches after a local `cmake --install` when possible.
# Packaging systems (RPM/Flatpak) handle this in their own post-install hooks.

find_program(UPDATE_DESKTOP_DATABASE update-desktop-database)
if(UPDATE_DESKTOP_DATABASE)
  set(_apps_dir "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/applications")
  if(EXISTS "${_apps_dir}")
    execute_process(COMMAND "${UPDATE_DESKTOP_DATABASE}" "${_apps_dir}"
                    RESULT_VARIABLE _r OUTPUT_QUIET ERROR_QUIET)
  endif()
endif()

find_program(GTK_UPDATE_ICON_CACHE gtk-update-icon-cache)
if(GTK_UPDATE_ICON_CACHE)
  set(_icon_dir "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/icons/hicolor")
  if(EXISTS "${_icon_dir}")
    execute_process(COMMAND "${GTK_UPDATE_ICON_CACHE}" -f -t "${_icon_dir}"
                    RESULT_VARIABLE _r OUTPUT_QUIET ERROR_QUIET)
  endif()
endif()
