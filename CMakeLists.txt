cmake_minimum_required(VERSION 4.2)
project(Flying_BRICKS VERSION 1.0.0 LANGUAGES CXX)

set(COMMON_LIB_F4VR_PATH "" CACHE PATH "CommonLibF4VR source checkout")
if(NOT EXISTS "${COMMON_LIB_F4VR_PATH}/CommonLibF4/include/F4SE/F4SE.h")
  message(FATAL_ERROR "Set COMMON_LIB_F4VR_PATH to the local CommonLibF4VR checkout")
endif()
set(ENABLE_FALLOUT_F4 OFF CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_NG OFF CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_VR ON CACHE BOOL "" FORCE)
set(F4SE_SUPPORT_XBYAK OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${COMMON_LIB_F4VR_PATH}/CommonLibF4" CommonLibF4 EXCLUDE_FROM_ALL)
set_property(TARGET CommonLibF4 PROPERTY INTERPROCEDURAL_OPTIMIZATION OFF)
target_compile_options(CommonLibF4 PRIVATE /wd4715)

configure_file(cmake/version.rc.in version.rc @ONLY)
# RPS Framework's staged package supplies the settings file (RPS::Settings).
set(RPSFramework_DIR "" CACHE PATH "RPS Framework staged package (build-fast/stage/lib/cmake/RPSFramework)")
find_package(RPSFramework CONFIG REQUIRED)
set(RPSUI_INCLUDE_DIR "" CACHE PATH "RPS UI Framework public include directory")
if(NOT EXISTS "${RPSUI_INCLUDE_DIR}/RPS_UI_Framework/WorldDraw.h")
  message(FATAL_ERROR "Provider header not found: ${RPSUI_INCLUDE_DIR}/RPS_UI_Framework/WorldDraw.h. Set RPSUI_INCLUDE_DIR.")
endif()

add_library(Flying_BRICKS SHARED
  src/Plugin.cpp src/AmmoTable.cpp src/Compatibility.cpp src/HitLabels.cpp src/Hooks.cpp src/Melee.cpp src/PatchFiles.cpp src/Presets.cpp src/Settings.cpp src/Terminal.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
# Header only, and vcpkg's port installs no CMake package for it.
find_path(TOMLPLUSPLUS_INCLUDE_DIR "toml++/toml.hpp" REQUIRED)
target_include_directories(Flying_BRICKS PRIVATE "${RPSUI_INCLUDE_DIR}")
target_compile_features(Flying_BRICKS PRIVATE cxx_std_23)
target_compile_definitions(Flying_BRICKS PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX NOMMNOSOUND UNICODE _UNICODE)
target_compile_options(Flying_BRICKS PRIVATE /W4 /wd4715 /utf-8 /Zi /permissive-)
target_link_options(Flying_BRICKS PRIVATE /DEBUG:FULL)
target_link_libraries(Flying_BRICKS PRIVATE CommonLibF4::CommonLibF4 RPS::Settings shell32.lib)
target_include_directories(Flying_BRICKS PRIVATE "${TOMLPLUSPLUS_INCLUDE_DIR}")
target_precompile_headers(Flying_BRICKS PRIVATE src/PCH.h)

# Tests: add executables whose target names contain "Tests" and register them with add_test.
# Keep a test only when it guards a real contract (see the master instructions).
enable_testing()
add_executable(PolicyTests tests/PolicyTests.cpp)
target_compile_features(PolicyTests PRIVATE cxx_std_23)
target_include_directories(PolicyTests PRIVATE src)
add_test(NAME PolicyTests COMMAND PolicyTests)
add_executable(PatchFormatTests tests/PatchFormatTests.cpp)
target_compile_features(PatchFormatTests PRIVATE cxx_std_23)
target_compile_options(PatchFormatTests PRIVATE /utf-8)
target_include_directories(PatchFormatTests PRIVATE src "${TOMLPLUSPLUS_INCLUDE_DIR}")
target_compile_definitions(PatchFormatTests PRIVATE PATCH_FOLDER="${CMAKE_CURRENT_SOURCE_DIR}/data/mod/F4SE/Plugins/Flying_BRICKS/Patches")
add_test(NAME PatchFormatTests COMMAND PatchFormatTests)
add_executable(SettingsTests tests/SettingsTests.cpp src/Settings.cpp)
target_compile_features(SettingsTests PRIVATE cxx_std_23)
target_compile_options(SettingsTests PRIVATE /utf-8)
target_include_directories(SettingsTests PRIVATE src)
target_compile_definitions(SettingsTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
  SETTINGS_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/settings-test")
target_link_libraries(SettingsTests PRIVATE RPS::Settings shell32.lib)
add_test(NAME SettingsTests COMMAND SettingsTests)

option(POST_BUILD_COPY_PLUGIN "Deploy only this DLL and PDB" OFF)
set(COPY_PLUGIN_BASE_PATH "" CACHE PATH "MO2 mod root for this plugin")
if(POST_BUILD_COPY_PLUGIN)
  if(COPY_PLUGIN_BASE_PATH STREQUAL "")
    message(FATAL_ERROR "COPY_PLUGIN_BASE_PATH is required for deployment")
  endif()
  add_custom_command(TARGET Flying_BRICKS POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${COPY_PLUGIN_BASE_PATH}/F4SE/Plugins"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:Flying_BRICKS>" "${COPY_PLUGIN_BASE_PATH}/F4SE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:Flying_BRICKS>" "${COPY_PLUGIN_BASE_PATH}/F4SE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/data/mod/Flying_BRICKS.esp" "${COPY_PLUGIN_BASE_PATH}/"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${COPY_PLUGIN_BASE_PATH}/F4SE/Plugins/RobCo_Patcher/leveledList"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/data/mod/F4SE/Plugins/RobCo_Patcher/leveledList/Flying_BRICKS.esp.ini" "${COPY_PLUGIN_BASE_PATH}/F4SE/Plugins/RobCo_Patcher/leveledList/"
    COMMENT "Deploying Flying_BRICKS DLL, PDB, ESP and RobCo Patcher file")
  # A target of its own, so that editing a patch file deploys it without relinking the DLL.
  add_custom_target(deploy_patch_files ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different "${CMAKE_CURRENT_SOURCE_DIR}/data/mod/F4SE/Plugins/Flying_BRICKS/Patches" "${COPY_PLUGIN_BASE_PATH}/F4SE/Plugins/Flying_BRICKS/Patches"
    COMMENT "Deploying Flying_BRICKS patch files")
endif()

# Tests share build-fast but stay out of the default build, so building and deploying the
# plugin never compiles them. Build them with `--target build_tests`, then run ctest.
function(fo4dev_collect_targets directory result)
  get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(child IN LISTS children)
    cmake_path(IS_PREFIX CMAKE_SOURCE_DIR "${child}" NORMALIZE child_is_ours)
    if(child_is_ours)
      fo4dev_collect_targets("${child}" child_targets)
      list(APPEND targets ${child_targets})
    endif()
  endforeach()
  set(${result} ${targets} PARENT_SCOPE)
endfunction()
fo4dev_collect_targets("${CMAKE_SOURCE_DIR}" fo4dev_targets)
add_custom_target(build_tests)
foreach(fo4dev_target IN LISTS fo4dev_targets)
  get_target_property(fo4dev_target_type ${fo4dev_target} TYPE)
  string(TOLOWER "${fo4dev_target}" fo4dev_target_name)
  if(fo4dev_target_name MATCHES "test" AND fo4dev_target_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|OBJECT_LIBRARY)$")
    set_target_properties(${fo4dev_target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    if(fo4dev_target_type STREQUAL "EXECUTABLE")
      add_dependencies(build_tests ${fo4dev_target})
    endif()
  endif()
endforeach()
