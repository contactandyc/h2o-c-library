# SPDX-FileCopyrightText: 2025-2026 Andy Curtis <contactandyc@gmail.com>
# SPDX-License-Identifier: Apache-2.0

# CMakeLists.txt for libraries (regular or header-only)
cmake_minimum_required(VERSION 3.20)

project(h2o_c_library
  VERSION 0.0.1
  LANGUAGES C CXX
)
# ── Variant selection for the umbrella alias (NOT for building) ───────────────
# We build ALL variant targets below; this just selects which one the umbrella
# alias (h2o_c_library::h2o_c_library) points to during this configure.
set(A_BUILD_VARIANT "debug" CACHE STRING
    "Umbrella choice for in-tree linking (debug|memory|static|shared)")
set_property(CACHE A_BUILD_VARIANT PROPERTY STRINGS debug memory static shared)

# ── Developer-only coverage toggle (applies to this entire CMake tree) ────────
option(A_ENABLE_COVERAGE "Enable code coverage instrumentation for this build" OFF)
if(A_ENABLE_COVERAGE)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_compile_options(-O0 -g -fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate -fcoverage-mapping)
  elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-O0 -g --coverage)
    add_link_options(--coverage)
  endif()
endif()

# Memory-profile convenience for the *_memory variant
option(A_BUILD_ENABLE_MEMORY_PROFILE "Define a macro on the 'memory' variant" OFF)
set(A_BUILD_MEMORY_DEFINE "_AML_DEBUG_" CACHE STRING
    "Macro to define on the 'memory' variant when memory profiling is enabled")

# Emulate Debug/Release per-variant (so one configure can build both kinds)
if(MSVC)
  set(_A_DEBUG_OPTS /Zi /Od)
  set(_A_RELEASE_OPTS /O2 /DNDEBUG)
else()
  set(_A_DEBUG_OPTS -O0 -g)
  set(_A_RELEASE_OPTS -O3 -DNDEBUG)
endif()

include(GNUInstallDirs)

# ---- Dependencies (Standard CMake) ----
find_package(libuv CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

# ---- Dependencies (PkgConfig Shims) ----
find_package(PkgConfig REQUIRED)
# Use IMPORTED_TARGET to automatically create PkgConfig::PC_H2O
pkg_check_modules(PC_H2O REQUIRED IMPORTED_TARGET libh2o)

# Alias 'h2o' to the robust PkgConfig target
if(NOT TARGET h2o)
  add_library(h2o ALIAS PkgConfig::PC_H2O)
endif()


# ── Library variants (ALL are defined & built/installed) ──────────────────────

add_library(h2o_c_library_debug STATIC
  src/h2o_c.c)

target_include_directories(h2o_c_library_debug PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(h2o_c_library_debug PROPERTIES
  C_STANDARD 23
  C_STANDARD_REQUIRED YES
  POSITION_INDEPENDENT_CODE ON
)

if("CXX" IN_LIST CMAKE_PROJECT_LANGUAGES)
  set_target_properties(h2o_c_library_debug PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
  )
endif()

# Link deps once
target_link_libraries(h2o_c_library_debug PUBLIC  h2o  libuv::uv  OpenSSL::Crypto  OpenSSL::SSL)

# Per-variant optimization flavor
target_compile_options(h2o_c_library_debug PRIVATE ${_A_DEBUG_OPTS})

# Memory profiling macro on the memory variant (opt-in)

# Install this variant
install(TARGETS h2o_c_library_debug EXPORT h2o_c_libraryTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

add_library(h2o_c_library_memory STATIC
  src/h2o_c.c)

target_include_directories(h2o_c_library_memory PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(h2o_c_library_memory PROPERTIES
  C_STANDARD 23
  C_STANDARD_REQUIRED YES
  POSITION_INDEPENDENT_CODE ON
)

if("CXX" IN_LIST CMAKE_PROJECT_LANGUAGES)
  set_target_properties(h2o_c_library_memory PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
  )
endif()

# Link deps once
target_link_libraries(h2o_c_library_memory PUBLIC  h2o  libuv::uv  OpenSSL::Crypto  OpenSSL::SSL)

# Per-variant optimization flavor
target_compile_options(h2o_c_library_memory PRIVATE ${_A_DEBUG_OPTS})

# Memory profiling macro on the memory variant (opt-in)
if(A_BUILD_ENABLE_MEMORY_PROFILE)
  target_compile_definitions(h2o_c_library_memory PUBLIC ${A_BUILD_MEMORY_DEFINE})
endif()

# Install this variant
install(TARGETS h2o_c_library_memory EXPORT h2o_c_libraryTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

add_library(h2o_c_library_static STATIC
  src/h2o_c.c)

target_include_directories(h2o_c_library_static PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(h2o_c_library_static PROPERTIES
  C_STANDARD 23
  C_STANDARD_REQUIRED YES
  POSITION_INDEPENDENT_CODE ON
)

if("CXX" IN_LIST CMAKE_PROJECT_LANGUAGES)
  set_target_properties(h2o_c_library_static PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
  )
endif()

# Link deps once
target_link_libraries(h2o_c_library_static PUBLIC  h2o  libuv::uv  OpenSSL::Crypto  OpenSSL::SSL)

# Per-variant optimization flavor
target_compile_options(h2o_c_library_static PRIVATE ${_A_RELEASE_OPTS})

# Memory profiling macro on the memory variant (opt-in)

# Install this variant
install(TARGETS h2o_c_library_static EXPORT h2o_c_libraryTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

add_library(h2o_c_library_shared SHARED
  src/h2o_c.c)

target_include_directories(h2o_c_library_shared PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(h2o_c_library_shared PROPERTIES
  C_STANDARD 23
  C_STANDARD_REQUIRED YES
  POSITION_INDEPENDENT_CODE ON
)

if("CXX" IN_LIST CMAKE_PROJECT_LANGUAGES)
  set_target_properties(h2o_c_library_shared PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
  )
endif()

# Link deps once
target_link_libraries(h2o_c_library_shared PUBLIC  h2o  libuv::uv  OpenSSL::Crypto  OpenSSL::SSL)

# Per-variant optimization flavor
target_compile_options(h2o_c_library_shared PRIVATE ${_A_RELEASE_OPTS})

# Memory profiling macro on the memory variant (opt-in)

# Install this variant
install(TARGETS h2o_c_library_shared EXPORT h2o_c_libraryTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# In-tree umbrella alias picking one variant (for unified builds)
string(REPLACE "-" "_" _variant_us "${A_BUILD_VARIANT}")
set(_sel_tgt "h2o_c_library_${_variant_us}")
if(TARGET "${_sel_tgt}")
  add_library(h2o_c_library::h2o_c_library ALIAS ${_sel_tgt})
endif()

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

# Export all variant targets together
install(EXPORT h2o_c_libraryTargets
  FILE h2o_c_libraryTargets.cmake
  NAMESPACE h2o_c_library::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/h2o_c_library)

# ---- Package config with umbrella target -------------------------------------
set(_CFG_IN "${CMAKE_CURRENT_BINARY_DIR}/config.cmake.in")
file(WRITE "${_CFG_IN}" [=[
@PACKAGE_INIT@
include(CMakeFindDependencyMacro)

if(NOT DEFINED A_BUILD_VARIANT OR A_BUILD_VARIANT STREQUAL "")
  set(A_BUILD_VARIANT "debug")
endif()
string(REPLACE "-" "_" _picked_us "${A_BUILD_VARIANT}")

set(_save_variant "${A_BUILD_VARIANT}")
set(_deps "@A_BUILD_DEPS@")
foreach(_dep IN LISTS _deps)
  if(_dep)
    find_dependency("${_dep}" REQUIRED)
  endif()
endforeach()
set(A_BUILD_VARIANT "${_save_variant}")

include("${CMAKE_CURRENT_LIST_DIR}/h2o_c_libraryTargets.cmake")

set(_ns "@A_BUILD_EXPORT_NAMESPACE@")
set(_pkg "@A_BUILD_TARGET_BASENAME@")
if(NOT TARGET ${_ns}::${_pkg})
  add_library(${_ns}::${_pkg} INTERFACE IMPORTED)
endif()

set(_pick ${_ns}::${_pkg}_${_picked_us})
if(TARGET "${_pick}")
  target_link_libraries(${_ns}::${_pkg} INTERFACE "${_pick}")
endif()
]=])

set(A_BUILD_TARGET_BASENAME "h2o_c_library")
set(A_BUILD_EXPORT_NAMESPACE "h2o_c_library")
set(A_BUILD_DEPS "libuv;OpenSSL")

include(CMakePackageConfigHelpers)
configure_package_config_file(
  "${_CFG_IN}"
  "${CMAKE_CURRENT_BINARY_DIR}/h2o_c_libraryConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/h2o_c_library
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/h2o_c_libraryConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/h2o_c_libraryConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/h2o_c_libraryConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/h2o_c_library
)
# Extra project-specific targets


enable_testing()
add_subdirectory(tests)
