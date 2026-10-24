cmake_minimum_required(VERSION 3.21)
project(WackGet VERSION 2.0.0 LANGUAGES C CXX)

option(WG_BUNDLED_CURL "Build libcurl from pinned source (default on Windows)" ${WIN32})
option(WG_WERROR "Treat warnings as errors (CI)" OFF)
option(WG_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer (gcc/clang)" OFF)
option(WG_FUZZ "Build libFuzzer targets (clang only)" OFF)
option(WG_GUI "Build the WackGet window (wxWidgets)" ON)
option(WG_BUNDLED_WX "Build wxWidgets from pinned source (default on Windows)" ${WIN32})

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Static C runtime on Windows: wackget.exe runs without installing the VC++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(cmake/hardening.cmake)
include(cmake/curl.cmake)

if(WG_FUZZ) # instrument the core too, not just the fuzz entry points
  add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------- core library
add_library(wgcore STATIC
  core/src/config.c
  core/src/dl.c
  core/src/fs.c
  core/src/mem.c
  core/src/queue.c
  core/src/text.c
  core/src/url.c)
target_include_directories(wgcore PUBLIC core/include)
target_link_libraries(wgcore PUBLIC CURL::libcurl)
if(WIN32)
  target_compile_definitions(wgcore PUBLIC UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(wgcore PUBLIC shell32 ole32 uuid)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(wgcore PUBLIC Threads::Threads)
endif()
wg_harden(wgcore)

# ---------------------------------------------------------------- command-line downloader (testing aid)
add_executable(wgdl tools/wgdl.c)
target_link_libraries(wgdl PRIVATE wgcore)
wg_harden(wgdl)

# ---------------------------------------------------------------- the window
if(WG_GUI AND NOT WG_FUZZ)
  include(cmake/wx.cmake)
  include(cmake/embed.cmake)
  wg_embed(${CMAKE_CURRENT_BINARY_DIR}/ui/icons.inc
           wackget_ico ${CMAKE_CURRENT_SOURCE_DIR}/WackGet/res/wackget.ico
           busy_ico ${CMAKE_CURRENT_SOURCE_DIR}/WackGet/res/busy.ico)
  set(ui_sources ui/app.cpp ui/dialogs.cpp ui/frame.cpp ui/platform.cpp ui/prefs.cpp ui/queue.cpp
                 ui/skin.cpp ui/view_native.cpp ui/view_skin.cpp)
  if(WIN32)
    list(APPEND ui_sources ui/res/wackget.rc)
  endif()
  add_executable(wackget WIN32 ${ui_sources})
  target_include_directories(wackget PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/ui)
  target_link_libraries(wackget PRIVATE wgcore)
  wg_wx_link(wackget)
  if(WIN32)
    target_link_libraries(wackget PRIVATE advapi32 credui dwmapi shell32 ole32)
    set_target_properties(wackget PROPERTIES OUTPUT_NAME WackGet)
    target_link_options(wackget PRIVATE /MANIFEST:NO) # the manifest comes from wx.rc
  else()
    # The Linux desktop (linux_desktop.cpp): GTK 3 and X11 directly for the tray menu and the hot
    # key; libayatana-appindicator3 is loaded at run time, so it is not needed to build.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0 x11)
    target_sources(wackget PRIVATE ui/linux_desktop.cpp)
    target_link_libraries(wackget PRIVATE PkgConfig::GTK3 ${CMAKE_DL_LIBS})
    pkg_check_modules(LIBSECRET IMPORTED_TARGET libsecret-1)
    if(LIBSECRET_FOUND)
      target_compile_definitions(wackget PRIVATE WG_HAVE_LIBSECRET)
      target_link_libraries(wackget PRIVATE PkgConfig::LIBSECRET)
    endif()
  endif()
  wg_harden(wackget)

  # ---------------------------------------------------------------- install + packages
  if(WIN32)
    install(TARGETS wackget RUNTIME DESTINATION .)
    install(FILES WackGet.html LICENSE.txt DESTINATION .)
  else()
    include(GNUInstallDirs)
    set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ
        GROUP_EXECUTE WORLD_READ WORLD_EXECUTE) # 0755 whatever the umask
    set(id io.github.levoniust.WackGet)
    set(docdir ${CMAKE_INSTALL_DATADIR}/doc/wackget) # Help looks in ../share/doc/wackget
    install(TARGETS wackget RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES linux/${id}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES linux/${id}.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    install(FILES linux/${id}.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    foreach(size 16 32 48 96 192)
      install(FILES linux/icons/wackget-${size}.png RENAME ${id}.png
              DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps)
    endforeach()
    install(FILES WackGet.html linux/copyright DESTINATION ${docdir})
    find_program(GZIP gzip)
    if(GZIP)
      foreach(f wackget.1 changelog)
        add_custom_command(OUTPUT ${f}.gz
                           COMMAND ${GZIP} -9nc ${CMAKE_CURRENT_SOURCE_DIR}/linux/${f} > ${f}.gz
                           DEPENDS linux/${f})
      endforeach()
      add_custom_target(linux_docs ALL DEPENDS wackget.1.gz changelog.gz)
      install(FILES ${CMAKE_CURRENT_BINARY_DIR}/wackget.1.gz DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
      install(FILES ${CMAKE_CURRENT_BINARY_DIR}/changelog.gz DESTINATION ${docdir})
    endif()

    # cpack -G DEB: a .deb for Debian 13 and Ubuntu 24.04+, dependencies worked out by dpkg-shlibdeps
    set(CPACK_GENERATOR DEB)
    set(CPACK_PACKAGE_NAME wackget)
    set(CPACK_PACKAGE_VENDOR "WackGet contributors")
    set(CPACK_PACKAGE_CONTACT "levoniust <33678948+levoniust@users.noreply.github.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "download manager: paste or drop links and let it fetch them")
    set(CPACK_PACKAGE_DESCRIPTION "WackGet is the classic 2002 download manager, rebuilt for today's
computers and kept looking and working the way it always did. Paste or drop
links into the queue and it downloads them a few at a time, resuming partial
files and retrying broken connections.")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/levoniust/WackGet")
    set(CPACK_DEBIAN_PACKAGE_SECTION net)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # the tray icon on Plasma/GNOME (loaded when present), and the .wgq association
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "libayatana-appindicator3-1, xdg-utils")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
    set(CPACK_STRIP_FILES ON)
    set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
    include(CPack)
  endif()
endif()

# ---------------------------------------------------------------- tests
enable_testing()

add_executable(wg_unit tests/unit.c)
target_link_libraries(wg_unit PRIVATE wgcore)
wg_harden(wg_unit)
add_test(NAME unit COMMAND wg_unit WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_executable(wg_engine tests/engine.c)
  target_link_libraries(wg_engine PRIVATE wgcore)
  wg_harden(wg_engine)
  add_test(NAME engine
           COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_engine.py $<TARGET_FILE:wg_engine>
           WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(engine PROPERTIES TIMEOUT 300)
endif()

# Fuzz targets: libFuzzer with clang (WG_FUZZ), otherwise a replay driver that runs the seed corpus
# plus random inputs, so every compiler exercises them in `ctest`.
foreach(target queue html text name)
  if(WG_FUZZ)
    add_executable(fuzz_${target} fuzz/fuzz_${target}.c)
    target_compile_options(fuzz_${target} PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(fuzz_${target} PRIVATE -fsanitize=fuzzer,address,undefined)
  else()
    add_executable(fuzz_${target} fuzz/fuzz_${target}.c fuzz/replay.c)
    wg_harden(fuzz_${target})
    add_test(NAME fuzz_replay_${target}
             COMMAND fuzz_${target} ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus/${target})
  endif()
  target_link_libraries(fuzz_${target} PRIVATE wgcore)
endforeach()
