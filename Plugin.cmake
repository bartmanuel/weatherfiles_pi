# ~~~
# Summary:      Local, non-generic plugin setup
# Copyright (c) 2020-2021 Mike Rossiter
# License:      GPLv3+
# ~~~

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.


# -------- Options ----------

set(OCPN_TEST_REPO
    "bartmanuel-fgsm/weatherfiles-alpha"
    CACHE STRING "Default repository for untagged builds"
)
set(OCPN_BETA_REPO
    "bartmanuel-fgsm/weatherfiles-beta"
    CACHE STRING
    "Default repository for tagged builds matching 'beta'"
)
set(OCPN_RELEASE_REPO
    "bartmanuel-fgsm/weatherfiles-prod"
    CACHE STRING
    "Default repository for tagged builds not matching 'beta'"
)

#
#
# -------  Plugin setup --------
#
set(PKG_NAME weatherfiles_pi)
set(PKG_VERSION  0.1.0)
set(PKG_PRERELEASE "")  # Empty, or a tag like 'beta'

set(DISPLAY_NAME Weatherfiles)  # dialogs, installer artifacts, ...
set(PLUGIN_API_NAME weatherfiles) # As of GetCommonName() in plugin API
set(PKG_SUMMARY "Browse WeatherFiles models and download GRIB file")
set(PKG_DESCRIPTION [=[
WeatherFiles plugin for OpenCPN: browse 27+ European weather models,
pick an area on the chart, and download sliced GRIB2 files that open
directly in the built-in GRIB display. Uses the WeatherFiles public
API (api.weatherfiles.com/v1) with a personal access token.
See developers.weatherfiles.com."
]=])

set(PKG_AUTHOR "Bart Manuel")
set(PKG_IS_OPEN_SOURCE "yes")
set(PKG_HOMEPAGE https://github.com/bartmanuel/weatherfiles_pi.git)
set(PKG_INFO_UR https://github.com/bartmanuel/weatherfiles_pi.git)  # FIXME: missing

set(SRC
    src/tpicons.cpp src/weatherfiles_pi.cpp
    src/wf_api.cpp src/wf_prefs_dialog.cpp src/wf_models_panel.cpp
    src/wf_download_dialog.cpp src/wf_download_progress.cpp
    src/wf_multi_slice_dialog.cpp
)

set(HDRS
    include/weatherfiles_pi.h
    include/tpicons.h
    include/wf_api.h
    include/wf_prefs_dialog.h
    include/wf_models_panel.h
    include/wf_download_dialog.h
    include/wf_download_progress.h
    include/wf_multi_slice_dialog.h
)


set(PKG_API_LIB api-18)  #  A dir in opencpn-libs/ e. g., api-17 or api-16

macro(late_init)
  # Perform initialization after the PACKAGE_NAME library, compilers
  # and ocpn::api is available.
  if (APPLE)
    target_compile_definitions(${PACKAGE_NAME} PUBLIC OCPN_GHC_FILESYSTEM)
  endif ()
  if (APPLE)
    option(OD_JSON_SCHEMA_VALIDATOR "Use JSON Schema validator" OFF)
  else (APPLE)
    option(OD_JSON_SCHEMA_VALIDATOR "Use JSON Schema validator" ON)
  endif (APPLE)
  target_include_directories(${PACKAGE_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/include)
  target_compile_definitions(${PACKAGE_NAME} PRIVATE PLUGIN_USE_SVG)
  if (WIN32)
    target_link_libraries(${PACKAGE_NAME} winhttp)
  endif ()
endmacro ()

macro(add_plugin_libraries)
  # Add libraries required by this plugin
  add_subdirectory("${CMAKE_SOURCE_DIR}/libs/std_filesystem")
  target_link_libraries(${PACKAGE_NAME} ocpn::filesystem)

  add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/tinyxml")
  target_link_libraries(${PACKAGE_NAME} ocpn::tinyxml)

  add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/wxJSON")
  target_link_libraries(${PACKAGE_NAME} ocpn::wxjson)

  add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/plugin_dc")
  target_link_libraries(${PACKAGE_NAME} ocpn::plugin-dc)

  add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/jsoncpp")
  target_link_libraries(${PACKAGE_NAME} ocpn::jsoncpp)

  # The wxsvg library enables SVG overall in the plugin
  add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/wxsvg")
  target_link_libraries(${PACKAGE_NAME} ocpn::wxsvg)

endmacro ()
