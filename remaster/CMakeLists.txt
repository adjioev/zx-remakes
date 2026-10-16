cmake_minimum_required(VERSION 3.20)
project(zx_remakes C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(RAYLIB REQUIRED IMPORTED_TARGET raylib)

add_executable(zxremakes
  src/main.cpp
  src/runner.cpp
  src/games.cpp
  src/ballbreaker_game.cpp
  src/ballbreaker_hd.cpp
  src/spectrum.cpp
  src/zxcore.c
  src/audio.cpp
  src/music_remix.cpp
  src/sfx.cpp
  src/gamepad.cpp
  src/gamefile.cpp)
option(APP_BUNDLE "Release build for the downloads (see .github/workflows/build.yml)" OFF)
if(APP_BUNDLE AND WIN32)
  # MSYS2/MinGW: raylib's DLLs are shipped next to the .exe
  target_link_libraries(zxremakes PRIVATE PkgConfig::RAYLIB -static-libgcc -static-libstdc++)
  set_target_properties(zxremakes PROPERTIES WIN32_EXECUTABLE ON)  # no console window
elseif(APP_BUNDLE AND NOT APPLE)
  # Linux: raylib static (its raylib.pc lists no system libs: GL/X11 are added here)
  target_include_directories(zxremakes PRIVATE ${RAYLIB_INCLUDE_DIRS})
  target_link_libraries(zxremakes PRIVATE ${RAYLIB_STATIC_LDFLAGS} GL X11 m pthread dl rt
    -static-libgcc -static-libstdc++)
else()
  target_link_libraries(zxremakes PRIVATE PkgConfig::RAYLIB)
endif()
if(APPLE)
  target_link_libraries(zxremakes PRIVATE "-framework Cocoa" "-framework IOKit" "-framework OpenGL")
endif()
target_compile_definitions(zxremakes PRIVATE PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(zxremakes PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>)
# the vendored emulator (third_party/chips) is built as it is, without our warnings
set_source_files_properties(src/zxcore.c PROPERTIES COMPILE_OPTIONS "-w")
