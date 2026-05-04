cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

include(FetchContent)

# PFFFT — BSD-like licence, compatible with GPL-2.0-or-later
# Version pin: commit 08f5ed2618ac06d7dcc83d209d7253dc215274d5 (hayguen fork)
# Fallback for offline builds: set FETCHCONTENT_SOURCE_DIR_PFFFT to a local clone
FetchContent_Declare(pffft
    GIT_REPOSITORY https://github.com/hayguen/pffft.git
    GIT_TAG        08f5ed2618ac06d7dcc83d209d7253dc215274d5
)
# PFFFT uses cmake_minimum_required(VERSION 2.8); CMake 4.0+ rejects < 3.5.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE INTERNAL "")
FetchContent_GetProperties(pffft)
if(NOT pffft_POPULATED)
  cmake_policy(SET CMP0169 OLD)
  FetchContent_Populate(pffft)
  cmake_policy(SET CMP0169 NEW)
  add_subdirectory(${pffft_SOURCE_DIR} ${pffft_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)

# PFFFT has benign warnings on MSVC; our project uses /WX, so suppress them
# for the vendored library to avoid build breakage.
if(MSVC)
  target_compile_options(PFFFT PRIVATE /wd4005 /wd4244 /wd4267 /wd4305 /wd4334 /wd4146 /wd4018)
endif()

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE PFFFT)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c src/av_sync_filter.c src/ring_buffer.c src/reference_tap.c src/gcc_phat.cpp src/smoother.cpp)

# Require C11 for <stdatomic.h> — MSVC needs /std:c11 or /std:c17 explicitly.
# obs-plugintemplate's compilerconfig sets C_STANDARD 17 globally; this is a
# belt-and-suspenders guard in case that changes or is absent.
set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY C_STANDARD 11)
set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY C_STANDARD_REQUIRED ON)

# MSVC does not enable C11 atomics under its default C mode.
# /std:c17 plus /experimental:c11atomics are required on MSVC 19.50.
if(MSVC)
  set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY C_STANDARD 17)
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /experimental:c11atomics)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# ── Testing ──────────────────────────────────────────────────────────────────
enable_testing()

# ThreadSanitizer option — Linux/macOS only (MSVC has no TSan equivalent).
option(ENABLE_TSAN "Enable ThreadSanitizer for testing (Linux/macOS only)" OFF)
if(ENABLE_TSAN)
  if(WIN32)
    message(WARNING "ThreadSanitizer is not available on Windows; ENABLE_TSAN ignored.")
  else()
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-fsanitize=thread" COMPILER_SUPPORTS_TSAN)
    if(NOT COMPILER_SUPPORTS_TSAN)
      message(WARNING "Compiler does not support -fsanitize=thread; ENABLE_TSAN ignored.")
    endif()
  endif()
endif()

add_subdirectory(tests)
