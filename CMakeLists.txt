cmake_minimum_required(VERSION 3.16)
project(comp_suite VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
include(GNUInstallDirs)

enable_testing()

set(CLAP_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/clap"
    CACHE PATH "Directory where CLAP bundles are installed")

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(LV2 REQUIRED IMPORTED_TARGET lv2)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)

set(COMP_UI_SOURCES src/ui/panel.cpp)

# ---------------------------------------------------------------------------
# CLAP plugin: six models, one X11/Cairo editor.
# ---------------------------------------------------------------------------
add_library(comp_suite MODULE
  src/clap/comp_suite_clap.cpp
  ${COMP_UI_SOURCES})
target_include_directories(comp_suite PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/third_party/clap/include")
target_link_libraries(comp_suite PRIVATE
  PkgConfig::CAIRO PkgConfig::FREETYPE PkgConfig::X11
  Threads::Threads ${CMAKE_DL_LIBS})
target_compile_features(comp_suite PRIVATE cxx_std_17)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(comp_suite PRIVATE -Wall -Wextra -fvisibility=hidden)
endif()
set_target_properties(comp_suite PROPERTIES
  PREFIX ""
  OUTPUT_NAME "CompSuite"
  SUFFIX ".clap"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clap")

set(COMP_RESOURCE_FILES
  "${CMAKE_SOURCE_DIR}/assets/fonts/Jost-ExtraBold.ttf"
  "${CMAKE_SOURCE_DIR}/assets/fonts/OFL.txt")

add_custom_target(comp_suite_bundle ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/clap/CompSuite.resources"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${COMP_RESOURCE_FILES}
          "${CMAKE_BINARY_DIR}/clap/CompSuite.resources/"
  DEPENDS comp_suite ${COMP_RESOURCE_FILES})

install(TARGETS comp_suite LIBRARY DESTINATION "${CLAP_DIR}")
install(FILES ${COMP_RESOURCE_FILES}
        DESTINATION "${CLAP_DIR}/CompSuite.resources")

# ---------------------------------------------------------------------------
# Tests and tools
# ---------------------------------------------------------------------------
add_executable(comp_dsp_test test/test_dsp.cpp)
target_include_directories(comp_dsp_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(comp_dsp_test PRIVATE cxx_std_17)
add_test(NAME comp_dsp COMMAND comp_dsp_test)

add_executable(comp_panel_test test/test_panel.cpp ${COMP_UI_SOURCES})
target_include_directories(comp_panel_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(comp_panel_test PRIVATE PkgConfig::CAIRO PkgConfig::FREETYPE)
target_compile_features(comp_panel_test PRIVATE cxx_std_17)
add_test(NAME comp_panel COMMAND comp_panel_test)

add_executable(comp_geometry_test test/test_geometry.cpp)
target_include_directories(comp_geometry_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(comp_geometry_test PRIVATE PkgConfig::CAIRO)
target_compile_features(comp_geometry_test PRIVATE cxx_std_17)
add_test(NAME comp_geometry COMMAND comp_geometry_test)

add_executable(comp_ui_preview test/ui_preview.cpp ${COMP_UI_SOURCES})
target_include_directories(comp_ui_preview PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(comp_ui_preview PRIVATE PkgConfig::CAIRO PkgConfig::FREETYPE)
target_compile_features(comp_ui_preview PRIVATE cxx_std_17)

find_library(XTST_LIBRARY Xtst)
find_program(XVFB_RUN xvfb-run)
add_executable(comp_clap_host_test test/clap_host_test.cpp)
target_include_directories(comp_clap_host_test PRIVATE "${CMAKE_SOURCE_DIR}/third_party/clap/include")
target_link_libraries(comp_clap_host_test PRIVATE PkgConfig::X11 ${CMAKE_DL_LIBS})
if(XTST_LIBRARY)
  target_link_libraries(comp_clap_host_test PRIVATE ${XTST_LIBRARY})
else()
  message(WARNING "libXtst not found: the UI drag test will be skipped")
endif()
add_dependencies(comp_clap_host_test comp_suite_bundle)
if(XVFB_RUN)
  add_test(NAME comp_clap_host
    COMMAND "${XVFB_RUN}" -a -s "-screen 0 1600x1000x24"
      "$<TARGET_FILE:comp_clap_host_test>" "${CMAKE_BINARY_DIR}/clap/CompSuite.clap")
else()
  add_test(NAME comp_clap_host
    COMMAND comp_clap_host_test "${CMAKE_BINARY_DIR}/clap/CompSuite.clap")
endif()

add_executable(comp_offline_render tools/offline_render.cpp)
target_include_directories(comp_offline_render PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(comp_offline_render PRIVATE cxx_std_17)

# ---------------------------------------------------------------------------
# LV2 build: same DSP and panel as the CLAP, exposed as an LV2 plugin + X11 UI.
# ---------------------------------------------------------------------------
pkg_check_modules(LILV QUIET IMPORTED_TARGET lilv-0)

set(LV2_BUNDLE_DIR "${CMAKE_BINARY_DIR}/lv2/comp-suite.lv2")

add_library(comp_suite_lv2 MODULE src/comp_suite_lv2.cpp)
target_include_directories(comp_suite_lv2 PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(comp_suite_lv2 PRIVATE PkgConfig::LV2)
target_compile_features(comp_suite_lv2 PRIVATE cxx_std_17)
set_target_properties(comp_suite_lv2 PROPERTIES
  PREFIX ""
  OUTPUT_NAME "comp_suite"
  LIBRARY_OUTPUT_DIRECTORY "${LV2_BUNDLE_DIR}")

add_library(comp_suite_lv2_ui MODULE src/ui/lv2_ui.cpp ${COMP_UI_SOURCES})
target_include_directories(comp_suite_lv2_ui PRIVATE
  "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/third_party/clap/include")
target_link_libraries(comp_suite_lv2_ui PRIVATE
  PkgConfig::CAIRO PkgConfig::FREETYPE PkgConfig::X11
  PkgConfig::LV2 Threads::Threads)
target_compile_features(comp_suite_lv2_ui PRIVATE cxx_std_17)
set_target_properties(comp_suite_lv2_ui PROPERTIES
  PREFIX ""
  OUTPUT_NAME "comp_suite_ui"
  LIBRARY_OUTPUT_DIRECTORY "${LV2_BUNDLE_DIR}")

add_custom_target(comp_suite_lv2_bundle ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${LV2_BUNDLE_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/lv2/manifest.ttl"
          "${CMAKE_SOURCE_DIR}/lv2/comp_suite.ttl"
          "${CMAKE_SOURCE_DIR}/assets/fonts/Jost-ExtraBold.ttf"
          "${CMAKE_SOURCE_DIR}/assets/fonts/OFL.txt"
          "${LV2_BUNDLE_DIR}/"
  DEPENDS comp_suite_lv2 comp_suite_lv2_ui)

install(TARGETS comp_suite_lv2 comp_suite_lv2_ui
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/lv2/comp-suite.lv2")
install(FILES
  lv2/manifest.ttl
  lv2/comp_suite.ttl
  assets/fonts/Jost-ExtraBold.ttf
  assets/fonts/OFL.txt
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/lv2/comp-suite.lv2")

if(LILV_FOUND)
  add_executable(comp_lv2_host_test test/lv2_host_test.cpp)
  target_include_directories(comp_lv2_host_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src" ${PKG_CONFIG_LV2_INCLUDE_DIRS})
  target_link_libraries(comp_lv2_host_test PRIVATE PkgConfig::LILV)
  add_dependencies(comp_lv2_host_test comp_suite_lv2_bundle)
  add_test(NAME comp_lv2_host
    COMMAND comp_lv2_host_test "${LV2_BUNDLE_DIR}")
endif()
