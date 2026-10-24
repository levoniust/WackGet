# wxWidgets for the window: the distro's package on Linux (Debian 13 has 3.2), or built from
# pinned source on Windows (3.3: dark mode, per-monitor DPI), statically, with only what WackGet uses.
# Defines wg_wx_link(target).

if(NOT WG_BUNDLED_WX)
  find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base net)
  function(wg_wx_link target)
    # SYSTEM: warnings inside wx headers are not ours (and would break -Werror)
    target_include_directories(${target} SYSTEM PRIVATE ${wxWidgets_INCLUDE_DIRS})
    target_compile_definitions(${target} PRIVATE ${wxWidgets_DEFINITIONS})
    separate_arguments(wx_cxx_flags UNIX_COMMAND "${wxWidgets_CXX_FLAGS}")
    target_compile_options(${target} PRIVATE ${wx_cxx_flags})
    target_link_libraries(${target} PRIVATE ${wxWidgets_LIBRARIES})
  endfunction()
  return()
endif()

cmake_minimum_required(VERSION 3.25) # FetchContent SYSTEM
include(FetchContent)

set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
set(wxBUILD_USE_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
set(wxBUILD_SAMPLES OFF CACHE STRING "" FORCE)
set(wxBUILD_TESTS OFF CACHE STRING "" FORCE)
set(wxBUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(wxBUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(wxBUILD_INSTALL OFF CACHE BOOL "" FORCE)
foreach(off wxUSE_WEBVIEW wxUSE_STC wxUSE_RICHTEXT wxUSE_PROPGRID wxUSE_RIBBON wxUSE_AUI wxUSE_GLCANVAS
            wxUSE_OPENGL wxUSE_MEDIACTRL wxUSE_XRC wxUSE_LIBTIFF wxUSE_LIBJPEG wxUSE_LIBWEBP
            wxUSE_LIBLZMA wxUSE_WEBREQUEST wxUSE_SOUND wxUSE_JOYSTICK)
  set(${off} OFF CACHE BOOL "" FORCE)
endforeach()

# wxWidgets 3.3.3 (2026-07-07), SHA-256 from the GitHub release asset digest
FetchContent_Declare(wxWidgets
  URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.3/wxWidgets-3.3.3.tar.bz2
  URL_HASH SHA256=81b09d6dd9f1ed9301f8c55a968a488d0491f264dc2bab19a7e407ac67009482
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  SYSTEM)
set(BUILD_TESTING_SAVED ${BUILD_TESTING})
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(wxWidgets)
set(BUILD_TESTING ${BUILD_TESTING_SAVED})

function(wg_wx_link target)
  target_link_libraries(${target} PRIVATE wx::core wx::base wx::net)
endfunction()
